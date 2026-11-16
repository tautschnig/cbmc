// asm goto in a file-local (static) function, built with goto-cc
// --export-file-local-symbols.  The Linux kernel's arch_static_branch (static
// keys) has exactly this shape.  The asm-goto lowering creates a temporary for
// the jump target; it used to be stored as a named sub of the asm code, which
// --export-file-local-symbols does not rename, so the renamed function body
// still referred to the unrenamed temporary and symex aborted with "failed to
// find ...::tmp_asm_goto_target".  Carrying the target as an operand makes the
// rename pass reach it, so symex succeeds.
struct static_key
{
  int enabled;
};

static inline __attribute__((always_inline)) _Bool
arch_static_branch(struct static_key *const key, const _Bool branch)
{
  asm goto(
    "1: jmp %l[l_yes]\n\t"
    ".pushsection __jump_table, \"aw\"\n\t"
    ".long 1b - . \n\t"
    ".popsection\n\t"
    :
    : "i"(key), "i"(branch)
    :
    : l_yes);
  return 0;
l_yes:
  return 1;
}

struct static_key kc;

int main(void)
{
  int x = arch_static_branch(&kc, 1) ? 1 : 0;
  __CPROVER_assert(x <= 1, "x");
  return 0;
}
