# PLR §6.3.1: int has a FINITE, non-extensible attribute set; `.foo` /
# `.foo()` on a provable-scalar int (literal or constant-folded symbol)
# raises AttributeError (ESBMC github_5904_attributeerror_{access,call}).
# Both the read and call paths flag it; real int methods
# (bit_length, ...) and UNTRACKED int-typed symbols (stub returns
# defaulted to int) are spared.
def access() -> None:
    x = 5
    y = x.foo  # AttributeError


def call() -> None:
    x = 5
    x.foo()  # AttributeError


access()
call()
