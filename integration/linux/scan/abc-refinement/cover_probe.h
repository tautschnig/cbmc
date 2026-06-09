// Cover-probe "assumption bisection" support header.
//
// A CHECKPOINT(id, cond) marks one hop of a CLAIMED path to a sink and the
// assumption the claim makes at that hop.  Under `cbmc --cover cover` each
// becomes a coverage goal: SATISFIED iff the program point is reachable AND
// `cond` is feasible there; FAILED otherwise.  Walking the checkpoints in
// path order, the FIRST FAILED is exactly where the claimed reasoning
// diverges from what the code allows (the "assumption bisection" point).
//
// REACH(id) is the pure-reachability probe (cond == 1): is this point
// reachable at all?
//
// When compiled by a normal C compiler the probes vanish, so the
// instrumented file still builds and runs.  The cover_probe.py driver
// passes `-DCBMC_PROBE` to CBMC to switch the probes on (CBMC does not
// predefine a detectable macro of its own here).
#ifndef COVER_PROBE_H
#define COVER_PROBE_H

#ifdef CBMC_PROBE
#define CHECKPOINT(id, cond) __CPROVER_cover((cond))
#define REACH(id) __CPROVER_cover(1)
#else
#define CHECKPOINT(id, cond) ((void)0)
#define REACH(id) ((void)0)
#endif

#endif
