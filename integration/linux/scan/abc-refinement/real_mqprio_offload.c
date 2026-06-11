#include <stdint.h>
#define TC_QOPT_MAX_QUEUE 16
// verbatim slice of mqprio_enable_offload (sch_mqprio.c:57-62): min_rate/
// max_rate copy loops bounded by qopt.num_tc (__u8 from netlink), arrays
// [TC_QOPT_MAX_QUEUE].  PRECOND = non-local validator mqprio_validate_qopt.
struct off { uint64_t min_rate[TC_QOPT_MAX_QUEUE];
             uint64_t max_rate[TC_QOPT_MAX_QUEUE]; uint8_t num_tc; };
struct priv { uint64_t min_rate[TC_QOPT_MAX_QUEUE];
              uint64_t max_rate[TC_QOPT_MAX_QUEUE]; };
int main(void){
  struct off mqprio; struct priv priv;
  uint8_t num_tc = mqprio.num_tc;
#ifdef PRECOND
  __CPROVER_assume(num_tc <= TC_QOPT_MAX_QUEUE);
#endif
  for (int i = 0; i < num_tc; i++) mqprio.min_rate[i] = priv.min_rate[i];
  for (int i = 0; i < num_tc; i++) mqprio.max_rate[i] = priv.max_rate[i];
  return 0;
}
