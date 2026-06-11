#include <stdint.h>
#define TC_MAX_QUEUE 16
#define TC_QOPT_MAX_QUEUE 16
// verbatim slice of taprio_change (sch_taprio.c:1900): q->cur_txq[i] =
// mqprio->offset[i] for i < mqprio->num_tc.  PRECOND = netdev_set_num_tc
// validator (num_tc <= TC_MAX_QUEUE).
struct mq { uint8_t num_tc; uint16_t count[TC_QOPT_MAX_QUEUE];
            uint16_t offset[TC_QOPT_MAX_QUEUE]; };
struct q { int cur_txq[TC_MAX_QUEUE]; };
int main(void){
  struct mq m; struct q q;
  uint8_t num_tc = m.num_tc;
#ifdef PRECOND
  __CPROVER_assume(num_tc <= TC_MAX_QUEUE);
#endif
  for (int i = 0; i < num_tc; i++) q.cur_txq[i] = m.offset[i];
  return 0;
}
