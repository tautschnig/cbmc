/* Slice: efa_create_cq_kernel() kvzalloc(sub_cqs_per_cq) (efa_verbs.c:1829-1830);
 * efa_create_qp_kernel(): efa_com_create_qp() -> res->send_sub_cq_idx =
 * cmd_completion.send_sub_cq_idx (u16, device) (efa_com_cmd.c:57) ->
 * efa_cq_inc_ref_cnt(send_cq, idx) (efa_verbs.c:1249, 701-703, u8 param). */
#include <stdlib.h>
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
struct efa_sub_cq
{
  u8 *buf;
  u32 cqe_size, queue_mask, ref_cnt, consumed_cnt;
  int phase;
};
struct efa_cq
{
  struct efa_sub_cq *sub_cq_arr;
};
u16 nondet_u16(void);
static void efa_cq_inc_ref_cnt(struct efa_cq *cq, u8 sub_cq_idx)
{
  cq->sub_cq_arr[sub_cq_idx].ref_cnt++;
}
int main(void)
{
  struct efa_cq cq;
  u16 sub_cqs_per_cq = nondet_u16(); /* dev_attr.sub_cqs_per_cq */
  __CPROVER_assume(sub_cqs_per_cq >= 1 && sub_cqs_per_cq <= 8);
  cq.sub_cq_arr = calloc(sub_cqs_per_cq, sizeof(*cq.sub_cq_arr));
  __CPROVER_assume(cq.sub_cq_arr != 0);
  u16 send_sub_cq_idx = nondet_u16(); /* create_qp completion */
  efa_cq_inc_ref_cnt(&cq, send_sub_cq_idx);
  return 0;
}
