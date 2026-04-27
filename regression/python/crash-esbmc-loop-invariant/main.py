# ESBMC-specific: __loop_invariant() verification primitive
# Not a Python language feature — ESBMC extension
def main():
    i: int = 0
    s: int = 0
    while i < 10:
        s += 1
        i += 1
    assert s == 10
main()
