#include <stdint.h>
#define CEC_MAX_LOG_ADDRS 4
// verbatim slice of cec_config_thread_func (cec-adap.c:1466..): write loop
// over log_addr[CEC_MAX_LOG_ADDRS] bounded by num_log_addrs (a struct field).
// PRECOND toggles the NON-LOCAL validator (cec-adap.c:1835:
// num_log_addrs <= available_log_addrs <= CEC_MAX_LOG_ADDRS).
struct la { uint8_t num_log_addrs; uint8_t log_addr[CEC_MAX_LOG_ADDRS]; };
int main(void) {
  struct la las;
#ifdef PRECOND
  __CPROVER_assume(las.num_log_addrs <= CEC_MAX_LOG_ADDRS); // the validator
#endif
  for (uint8_t i = 0; i < las.num_log_addrs; i++)
    las.log_addr[i] = 0;                     // the flagged write
  return 0;
}
