# PLR §6.2 evaluation-once: `xs.extend(src())` evaluates src() EXACTLY
# once. The per-slot copy loop embedded the argument expression ~35
# times, so a CALL argument executed the callee once per use -- wrong
# for side-effecting arguments (the call counter below) and the root of
# a 486x loop-unwind blowup on aws_untagged (144 emitted calls for one
# source call; program expression 4.2M -> 211K steps after the fix).
# Non-symbol list-typed args are materialised into a temp; string
# constants keep the compile-time fold (list_extend5).
calls = [0]


def src() -> list:
    calls[0] = calls[0] + 1
    return [1, 2]


xs: list = []
xs.extend(src())
assert calls[0] == 1
assert len(xs) == 2
