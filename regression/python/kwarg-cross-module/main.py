# Keyword arguments to imported functions are now bound (were dropped/nondet).
import mod

assert mod.foo(a=5) == 10          # pure keyword
assert mod.bar(b=1, a=10) == 9     # out-of-order keywords
assert mod.bar(10, b=1) == 9       # positional + keyword
