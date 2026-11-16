// asm goto in a static function, built with goto-cc
// --export-file-local-symbols.  The kernel's arch_static_branch (static
// keys) is exactly this shape, and every Linux harness that calls a static
// driver function needs the flag.  See asm_goto_file_local_rename.md.
struct static_key
{
  int enabled;
};
static inline __attribute__((always_inline)) _Bool
arch_static_branch(struct static_key *const key, const _Bool branch)
{
  asm goto("1: jmp %l[l_yes]\n\t"
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
