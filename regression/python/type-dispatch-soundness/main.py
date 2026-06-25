# Soundness (P4 audit lock-in): isinstance / type narrowing / inheritance /
# virtual dispatch / hasattr must not false-prove. Each assertion is genuinely
# FALSE, so verification must FAIL.
class A:
    def f(self):
        return 1
class B(A):
    def f(self):
        return 2

assert isinstance(5, str)            # int is not str
assert not isinstance(B(), A)        # B is an A
x = "hello"
if isinstance(x, str):
    assert len(x) == 99              # narrowed-to-str but wrong length
y: A = B()
assert y.f() == 1                    # virtual dispatch resolves to B.f -> 2
assert hasattr(5, "foo")             # int has no attribute foo
