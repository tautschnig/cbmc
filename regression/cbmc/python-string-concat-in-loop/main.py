# PLR §6.5.6 (str.__add__) inside loops: the cprover_string_*
# intrinsic calls take the result string by output args
# (__string_len_N, __string_ptr_N) which are emitted once per
# AST node. When the AST node is in a loop body, the same
# output symbols are reused across iterations. Without an
# explicit havoc, the SSA-encoding gives them a single SSA
# version, and the string-refinement backend stacks
# conflicting per-iteration constraints into UNSAT — which
# made any assertion verify SUCCESSFUL regardless of
# correctness.
#
# Fix: havoc the output args before each call.

def for_loop_concat_correct() -> None:
    s = ""
    for c in ["a", "b", "c"]:
        s = s + c
    assert s == "abc"


def for_loop_concat_int_chars() -> None:
    s = ""
    for n in range(3):
        s = s + str(n)
    assert s == "012"


for_loop_concat_correct()
for_loop_concat_int_chars()
