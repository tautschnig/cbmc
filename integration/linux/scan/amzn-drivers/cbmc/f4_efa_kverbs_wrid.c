/* Slice: efa_poll_sub_cq() (amzn-drivers efa_data_verbs.c:600-683).
 * wq->wrid / wrid_idx_pool are kvzalloc(max_wqes) (efa_verbs.c:1087-1091);
 * cqe->req_id is a u16 written by the device into the CQ ring. */
#include <stdlib.h>
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
struct efa_wq
{
  u64 *wrid;
  u32 *wrid_idx_pool;
  u32 wrid_idx_pool_next;
  u32 max_wqes;
};
u16 nondet_u16(void);
u32 nondet_u32(void);
int main(void)
{
  struct efa_wq wq;
  wq.max_wqes = nondet_u32();
  __CPROVER_assume(wq.max_wqes >= 1 && wq.max_wqes <= 64);
  wq.wrid = malloc(wq.max_wqes * sizeof(u64));
  wq.wrid_idx_pool = malloc(wq.max_wqes * sizeof(u32));
  __CPROVER_assume(wq.wrid && wq.wrid_idx_pool);
  wq.wrid_idx_pool_next = nondet_u32();
  __CPROVER_assume(wq.wrid_idx_pool_next <= wq.max_wqes); /* any #outstanding */
  u32 wrid_idx = nondet_u16();                            /* cqe->req_id */
  wq.wrid_idx_pool_next--;
  wq.wrid_idx_pool[wq.wrid_idx_pool_next] = wrid_idx;
  u64 wr_id = wq.wrid[wrid_idx];
  return (int)wr_id;
}
