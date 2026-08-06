# --python-unbounded-ints: int/float boundaries and the pow lowering.
# Previously: 2 ** n (symbolic exponent) crashed the SMT2 conversion
# (Int -> float typecast unsupported off the FPA theory), true
# division crashed the same way, and several representation members
# read as integer_typet crashed simplify_member.
n = 3
assert 2 ** n == 8
m = -1
r = 2 ** m
assert r == 0.5                  # PLR 6.5: negative exponent -> float
assert 10 ** 20 > 10 ** 19       # exact beyond 64 bits
big = 2 ** 100
assert big == 1267650600228229401496703205376

a = 7
b = 2
c = a / b
assert c == 3.5                  # PLR 6.7: true division -> float

xs = [1, 2, 3, 4, 5]
cursor = 0
nxt = cursor + len(xs[cursor:cursor + 2])
assert nxt == 2                  # len() result mixes with math ints


def make_backoff(base_ms: int):
    def step(attempt: int) -> int:
        return base_ms * (2 ** attempt)
    return step


backoff = make_backoff(50)
attempt = 0
while attempt < 3:
    attempt += 1
    assert backoff(attempt) == 50 * (2 ** attempt)
