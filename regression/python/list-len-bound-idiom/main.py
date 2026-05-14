# Path-sensitive list-length idiom for index bounds.
#
# When `len(L) >= N and L[i] ...` is the test of an if (or
# any short-circuiting `and`), the second operand only runs
# when len(L) >= N is true, so L[i] for i < N is safe. The
# frontend recognises this in convert_bool_op and elides the
# IndexError check at the constant-index subscript.

def first_three(parts: list) -> bool:
    if len(parts) >= 3 and parts[1] == 'execute-api':
        return True
    return False


# Multiple bounds in the same chain.
def two_lists(a: list, b: list) -> int:
    if len(a) >= 2 and len(b) >= 1 and a[1] + b[0] > 0:
        return a[0]
    return -1


# Reversed forms: N <= len(L), N < len(L), len(L) > N.
def reversed_forms(s: list) -> int:
    if 4 <= len(s) and s[3] == 'x':
        return 0
    if 2 < len(s) and s[2] == 'y':
        return 1
    if len(s) > 5 and s[5] == 'z':
        return 2
    return -1


# Sanity: a clearly out-of-bounds access without a guard
# remains a violation. Disabled here (would FAIL); the test
# expects VERIFICATION SUCCESSFUL on the safe paths.
xs: list[int] = [10, 20, 30]
if len(xs) >= 3 and xs[2] == 30:
    pass
