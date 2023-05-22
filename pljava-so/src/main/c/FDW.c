/**
 * Initial stab at the minimal requirements for a read-only Foreign Data Wrapper.
 *
 * This implementation will need to add support for options. The first option
 * would be the identity of the java class that implements the required methods.
 * The remaining options would be provided to that class via a TBD mechanism.
 *
 * The minimum required implementation is 7 methods:
 *
 *  - getForeignRelSize
 *  - getForeignPaths
 *  - getForeignPlan
 *  - ForeignScan:
 *      - begin
 *      - iterate
 *      - rescan
 *      - end
 *
 * and to a large extent they only need to handle three notable functions.
 *
 * extern ForeignPath *create_foreignscan_path(
 *     PlannerInfo *root,     -- passthrough
 *     RelOptInfo *rel,       -- passthrough
 *     PathTarget *target,
 *     double rows,
 *     Cost startup_cost,
 *     Cost total_cost,
 *     List *pathkeys,
 *     Relids required_outer,
 *     Path *fdw_outerpath,
 *     List *fdw_private);    -- the java integration 
 *
 * extern ForeignScan *make_foreignscan(
 *     List *qptlist,         -- passthrough
 *     List *qpqual,          -- passthrough
 *     Index scanrelid,       -- passthrough
 *     List *fdw_exprs,
 *     List *fdw_private,     -- the java integration
 *     List *fdw_scan_tlist,
 *     List *fdw_recheck_quals,
 *     Plan *outer_plan);     -- passthrough
 *
 * plus a function in 'iterate' that creates the resulting value.
 *
 * Credit/blame: this borrows heavily from https://github.com/slaught/pljava_fdw/blob/master/pljava_data.c
 */

#include "postgres.h"
#include "access/sysattr.h"
#include "nodes/pg_list.h"
#include "nodes/makefuncs.h"
#include "catalog/pg_foreign_server.h"
#include "catalog/pg_foreign_table.h"
#include "catalog/pg_type.h" 
#include "foreign/fdwapi.h"
#include "foreign/foreign.h"
#include "commands/explain.h"
#include "optimizer/paths.h"
#include "optimizer/pathnode.h"
#include "optimizer/planmain.h"
#include "parser/parsetree.h"
#include "optimizer/restrictinfo.h"


PG_MODULE_MAGIC;

extern Datum pljava_handler(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(pljava_handler);

void        _PG_init(void);
void        _PG_fini(void);

void _PG_init() {
}

void _PG_fini() {
}

/*
 * FDW functions declarations
 */

static void pljava_GetForeignRelSize(PlannerInfo *root,
                           RelOptInfo *baserel,
                           Oid foreigntableid);
static void pljava_GetForeignPaths(PlannerInfo *root,
                         RelOptInfo *baserel,
                         Oid foreigntableid);
#if (PG_VERSION_NUM <= 90500)
static ForeignScan *pljava_GetForeignPlan(PlannerInfo *root,
                        RelOptInfo *baserel,
                        Oid foreigntableid,
                        ForeignPath *best_path,
                        List *tlist,
                        List *scan_clauses);

#else
static ForeignScan *pljava_GetForeignPlan(PlannerInfo *root,
                        RelOptInfo *baserel,
                        Oid foreigntableid,
                        ForeignPath *best_path,
                        List *tlist,
                        List *scan_clauses,
                        Plan *outer_plan);
#endif

static void pljava_BeginForeignScan(ForeignScanState *node, int eflags);
static TupleTableSlot *pljava_IterateForeignScan(ForeignScanState *node);
static void pljava_ReScanForeignScan(ForeignScanState *node);
static void pljava_EndForeignScan(ForeignScanState *node);

Datum pljava_handler(PG_FUNCTION_ARGS) {
    FdwRoutine *fdw_routine = makeNode(FdwRoutine);

    fdw_routine->GetForeignRelSize = pljava_GetForeignRelSize;
    fdw_routine->GetForeignPaths = pljava_GetForeignPaths;
    fdw_routine->GetForeignPlan = pljava_GetForeignPlan;
    fdw_routine->BeginForeignScan = pljava_BeginForeignScan;
    fdw_routine->IterateForeignScan = pljava_IterateForeignScan;
    fdw_routine->ReScanForeignScan = pljava_ReScanForeignScan;
    fdw_routine->EndForeignScan = pljava_EndForeignScan;

    /* optional */
    fdw_routine->ExplainForeignScan = null;
    fdw_routine->ExplainForeignModify = null;

    /* insert support */
    fdw_routine->AddForeignUpdateTargets = null;

    fdw_routine->PlanForeignModify = null;
    fdw_routine->BeginForeignModify = null;
    fdw_routine->ExecForeignInsert = null;
    fdw_routine->ExecForeignUpdate = null;
    fdw_routine->ExecForeignDelete = null;
    fdw_routine->EndForeignModify = null;
    fdw_routine->IsForeignRelUpdatable = null;

    fdw_routine->AnalyzeForeignTable = null;

    PG_RETURN_POINTER(fdw_routine);
}

// create_foreignscan_path()
// extract_actual_clauses()
// makeConst()
// make_foreignscan()
// add_path()

/*
 * GetForeignRelSize
 *        set relation size estimates for a foreign table
 */
static void
pljava_GetForeignRelSize(PlannerInfo *root,
                       RelOptInfo *baserel,
                       Oid foreigntableid) {
    baserel-> rows = 0;
}

/*
 * GetForeignPaths
 *        create access path for a scan on the foreign table
 */
static void
pljava_GetForeignPaths(PlannerInfo *root,
                     RelOptInfo *baserel,
                     Oid foreigntableid) {
    Path *path;
#if (PG_VERSION_NUM < 90500)
    path = (Path *) create_foreignscan_path(root, baserel,
                        baserel->rows,
                        10,
                        0,
                        NIL,    
                        NULL,    
                        NULL);
#else
    path = (Path *) create_foreignscan_path(root, baserel,
#if PG_VERSION_NUM >= 90600
                        NULL,
#endif
                        baserel->rows,
                        10,
                        0,
                        NIL,
                        NULL,
                        NULL,
                        NIL);
#endif
    add_path(baserel, path);
}

/*
 * GetForeignPlan
 *    create a ForeignScan plan node 
 */
#if (PG_VERSION_NUM <= 90500)
static ForeignScan *
pljava_GetForeignPlan(PlannerInfo *root,
                    RelOptInfo *baserel,
                    Oid foreigntableid,
                    ForeignPath *best_path,
                    List *tlist,
                    List *scan_clauses) {
    Index scan_relid = baserel->relid;
    Datum blob = 0;
    Const *blob2 = makeConst(INTERNALOID, 0, 0,
                 sizeof(blob),
                 blob,
                 false, false);
    scan_clauses = extract_actual_clauses(scan_clauses, false);
    return make_foreignscan(tlist,
            scan_clauses,
            scan_relid,
            scan_clauses,        
            (void *)blob2);
}
#else
static ForeignScan *
pljava_GetForeignPlan(PlannerInfo *root,
                    RelOptInfo *baserel,
                    Oid foreigntableid,
                    ForeignPath *best_path,
                    List *tlist,
                    List *scan_clauses,
                    Plan *outer_plan) {
    Index scan_relid = baserel->relid;
    scan_clauses = extract_actual_clauses(scan_clauses, false);

    return make_foreignscan(tlist,
            scan_clauses,
            scan_relid,
            scan_clauses,
            NIL,
            NIL,
            NIL,
            outer_plan);
}
#endif

/*
 * BeginForeignScan
 *   called during executor startup. perform any initialization 
 *   needed, but not start the actual scan. 
 */
static void
pljava_BeginForeignScan(ForeignScanState *node, int eflags) {
}

/*
 * IterateForeignScan
 *        Retrieve next row from the result set, or clear tuple slot to indicate
 *        EOF.
 *   Fetch one row from the foreign source, returning it in a tuple table slot 
 *    (the node's ScanTupleSlot should be used for this purpose). 
 *  Return NULL if no more rows are available. 
 */
static TupleTableSlot *
pljava_IterateForeignScan(ForeignScanState *node) {
    return NULL;
}

/*
 * ReScanForeignScan
 *        Restart the scan from the beginning
 */
static void
pljava_ReScanForeignScan(ForeignScanState *node) {
}

/*
 *EndForeignScan
 *    End the scan and release resources. 
 */
static void
pljava_EndForeignScan(ForeignScanState *node) {
}
