/* Minimal reproducer for issue B of doc/dfcc-issues-2026-11.md.
 *
 * Under --dfcc --apply-loop-contracts, assignments to variables LOCAL to a
 * callee are checked against the CALLER's loop assigns clause, which the
 * caller cannot satisfy because callee locals are not nameable in its
 * contract.  Requires a loop inside the callee; see the .sh for the controls.
 *
 *   [callee.assigns.1] Check that a is assignable: FAILURE   <- callee local
 *   [callee.assigns.2] Check that k is assignable: FAILURE   <- its loop counter
 *   [callee.assigns.3] Check that g[0] is assignable: SUCCESS
 */
static int g[4];
static void callee(int v){ int a=0; for(int k=0;k<3;k++){ a=a+v; } g[0]=a; }
unsigned nd(void){unsigned v; return v;}
int main(void){ unsigned n=nd();
  for(unsigned i=0;i<n;i++)
    __CPROVER_assigns(i, __CPROVER_object_whole(g))
    __CPROVER_loop_invariant(i<=n)
    __CPROVER_decreases(n-i)
  { callee((int)i); }
  return 0; }
