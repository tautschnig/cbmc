/*
 * rte_pktmbuf_read is `static inline` in rte_mbuf.h.  The harness translation
 * unit records the symbol without a body (it never calls it directly), and
 * the call from ena_xmit_mbuf's LLQ push path resolves to that bodyless copy.
 * Under DFCC a bodyless callee is turned into assert(false); assume(false),
 * which CUTS the path: the obligations are then not checked on the LLQ-push
 * sub-path, and by DFCC issue A that happens silently.
 *
 * Taking the function's address from the harness does not force emission, so
 * the body is supplied here instead, in its own translation unit, the same
 * way rdtsc_stub.c supplies __builtin_ia32_rdtsc.
 *
 * This is NOT a stub.  Renaming the header's inline definition and then
 * forwarding to it means the proof runs DPDK's real implementation, including
 * its contiguous/non-contiguous branch, so nothing is abstracted away.
 */

#define rte_pktmbuf_read dpdk_rte_pktmbuf_read_inline
#include <rte_mbuf.h>
#include <string.h>
#undef rte_pktmbuf_read

const void *rte_pktmbuf_read(
  const struct rte_mbuf *m,
  uint32_t off,
  uint32_t len,
  void *buf)
{
  return dpdk_rte_pktmbuf_read_inline(m, off, len, buf);
}

/*
 * The non-contiguous helper that the real body falls back to.  It is extern
 * in DPDK (defined in rte_mbuf.c, which the proof does not link), so left
 * alone it would reintroduce exactly the same path cut one level down.
 *
 * Modelled faithfully with respect to what the proof depends on: the call
 * copies len bytes of packet data into the caller's buffer and returns it.
 * The byte values are irrelevant to every obligation -- no invariant reads
 * packet payload -- but the copy must stay inside buf, so the write is
 * performed at full width and left for the bounds checks to police rather
 * than being assumed away.
 */
const void *__rte_pktmbuf_read(
  const struct rte_mbuf *m,
  uint32_t off,
  uint32_t len,
  void *buf)
{
  if(off + len > rte_pktmbuf_pkt_len(m))
    return NULL;

  // A byte loop here would need unwinding proportional to the copy length and
  // dominates the whole proof; memcpy is both faithful to the real routine and
  // handled natively, so the bounds checks still police the copy.
  memcpy(buf, rte_pktmbuf_mtod_offset(m, char *, off), len);

  return buf;
}
