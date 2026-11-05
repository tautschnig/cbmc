/* __builtin_ia32_rdtsc has no body in the goto model, and under DFCC a
 * bodyless callee becomes an unreachable stub.  rte_get_timer_cycles() ->
 * rte_rdtsc() -> __rdtsc() -> this builtin sits on ena_xmit_mbuf's SUCCESS
 * path (tx_info->timestamp, ena_ethdev.c:3187), so leaving it bodyless makes
 * the whole accept path unreachable and every accept-path obligation vacuous.
 *
 * It cannot be stubbed with -D (that collides with gcc's own __rdtsc
 * definition in ia32intrin.h) nor from inside the harness translation unit,
 * so it is compiled separately and linked in.  A timestamp the driver only
 * stores and later compares is sound to model as any value; 0 is used
 * because nothing in the TX path depends on it. */
unsigned long long __builtin_ia32_rdtsc(void)
{
  return 0;
}
