# PLR §8.7 — leaf functions whose body is a single `return <constant>`
# can be propagated through downstream constant folding. Without this,
# code like
#
#   def return_int() -> int:
#       return 97
#   c = return_int()
#   assert chr(c) == "a"
#
# trips chr()'s symbolic-input fallback (a single-byte string with a
# non-constant byte), and the assertion is unprovable. With the
# function_return_constants tracking, try_eval_double sees the call
# and substitutes 97 at conversion time, so chr() folds to "a".

def return_int() -> int:
    return 97

c = return_int()
assert chr(c) == "a"

# Negative-literal return body: covered via UnaryOp(USub, Constant)
def neg_seven() -> int:
    return -7

x = neg_seven()
assert x == -7
