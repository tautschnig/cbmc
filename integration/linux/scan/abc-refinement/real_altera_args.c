#include <stdint.h>
// verbatim slice of altera_execute (altera.c:516-520): the args[3] write
// loop whose bound arg_count is masked to <=3.  CBMC --bounds-check is the
// DEFINITIVE filter upgrading the adv_mask=MASKED heuristic to a proof.
int main(void) {
  uint8_t p[260];
  uint32_t pc;
  __CPROVER_assume(pc <= 255);
  uint32_t opcode = p[pc] & 0xff;          // bytecode byte
  uint32_t arg_count = (opcode >> 6) & 3;  // <= 3
  uint32_t args[3];
  for (uint32_t i = 0; i < arg_count; ++i) {
    args[i] = ((uint32_t)p[pc] << 24);     // the flagged write
  }
  return 0;
}
