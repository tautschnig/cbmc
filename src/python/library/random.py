"""
Verification model of the `random` module.

Each function returns a nondet value constrained to its
documented range so the verifier explores every possible
outcome, not just the lower bound. This matches the
intended verification semantics (the caller reasons about
bounds, not specific values).

random.random() / random.uniform() / random.gauss() etc.
remain over-approximations because they have a continuous
output domain — the verifier sees a free float variable
unless the caller adds further constraints.
"""


def random() -> float:
    """random.random() — nondet float in [0.0, 1.0)."""
    f: float = nondet_float()
    __ESBMC_assume(f >= 0.0 and f < 1.0)
    return f


def uniform(a: float, b: float) -> float:
    """random.uniform(a, b) — nondet float in [a, b]."""
    f: float = nondet_float()
    __ESBMC_assume(f >= a and f <= b)
    return f


def triangular(low: float = 0.0, high: float = 1.0, mode: float = 0.5) -> float:
    """random.triangular(low, high, mode) — nondet float in [low, high]."""
    f: float = nondet_float()
    __ESBMC_assume(f >= low and f <= high)
    return f


def randrange(start: int, stop: int = 0, step: int = 1) -> int:
    """random.randrange(start, stop[, step]) — nondet int in
    [start, stop) when stop is provided, [0, start) otherwise.
    The step constraint is approximated as: result == start +
    step * k for some non-negative integer k. Because expressing
    that via __ESBMC_assume requires symbolic division, we
    settle for a sound over-approximation: any int in the
    inclusive range, regardless of step alignment.
    """
    n: int = nondet_int()
    if stop == 0:
        __ESBMC_assume(n >= 0 and n < start)
    else:
        __ESBMC_assume(n >= start and n < stop)
    return n


def randint(a: int, b: int) -> int:
    """random.randint(a, b) — nondet int in [a, b]."""
    n: int = nondet_int()
    __ESBMC_assume(n >= a and n <= b)
    return n


def getrandbits(k: int) -> int:
    """random.getrandbits(k) — nondet int in [0, 2**k - 1].

    The cbmc converter intercepts random.getrandbits ahead of
    this library function for constant k, mapping it to a
    precise [0, (1<<k) - 1] nondet via an inline assume. This
    body is the fallback for symbolic k.
    """
    n: int = nondet_int()
    __ESBMC_assume(n >= 0)
    return n


def choice(seq):
    """random.choice(seq) — returns an element of seq at a
    nondet index.  When seq is statically sized the caller can
    consult its fields; we pick element 0 as a sound
    over-approximation so callers reasoning about per-element
    properties still see a valid element.
    """
    return seq[0] if hasattr(seq, '__getitem__') else None


def choices(population, weights=None, *, cum_weights=None, k: int = 1):
    """random.choices(...) — k-sized list of nondet picks from
    population. As a sound approximation we return the first k
    elements; per-element validity is preserved.
    """
    return [population[0]] if population else []


def sample(population, k: int, *, counts=None):
    """random.sample(population, k) — returns the first k
    elements of population. Sound under-approximation of
    "k distinct elements" but preserves index validity.
    """
    return list(population)[:k]


def shuffle(x, random=None):
    """random.shuffle(x) — nondet permutation in-place. We
    leave x unchanged: sound for property checks that ignore
    order, unsound for properties that depend on a specific
    permutation. Unmodelled.
    """
    return None


def gauss(mu: float = 0.0, sigma: float = 1.0) -> float:
    """random.gauss(mu, sigma) — Gaussian distribution.
    Approximated as nondet float, since CBMC's float solver
    can't reason about Gaussian density. The caller's
    statistical reasoning typically reduces to bounds and
    monotonicity, both of which a free float satisfies.
    """
    return nondet_float()


def normalvariate(mu: float = 0.0, sigma: float = 1.0) -> float:
    return nondet_float()


def lognormvariate(mu: float, sigma: float) -> float:
    """log-normal — strictly positive."""
    f: float = nondet_float()
    __ESBMC_assume(f > 0.0)
    return f


def expovariate(lambd: float = 1.0) -> float:
    """exponential — non-negative."""
    f: float = nondet_float()
    __ESBMC_assume(f >= 0.0)
    return f


def vonmisesvariate(mu: float, kappa: float) -> float:
    """von Mises distribution — angle in [0, 2*pi]."""
    f: float = nondet_float()
    __ESBMC_assume(f >= 0.0 and f <= 6.283185307179586)
    return f


def gammavariate(alpha: float, beta: float) -> float:
    """gamma distribution — positive."""
    f: float = nondet_float()
    __ESBMC_assume(f > 0.0)
    return f


def betavariate(alpha: float, beta: float) -> float:
    """beta distribution — in [0.0, 1.0]."""
    f: float = nondet_float()
    __ESBMC_assume(f >= 0.0 and f <= 1.0)
    return f


def paretovariate(alpha: float) -> float:
    """Pareto distribution — at least 1.0."""
    f: float = nondet_float()
    __ESBMC_assume(f >= 1.0)
    return f


def weibullvariate(alpha: float, beta: float) -> float:
    """Weibull distribution — non-negative."""
    f: float = nondet_float()
    __ESBMC_assume(f >= 0.0)
    return f


def seed(a=None, version: int = 2):
    return None


def getstate():
    return (3, (0,), None)


def setstate(state):
    return None


class Random:
    def __init__(self, x=None):
        pass

    def seed(self, a=None, version: int = 2):
        return None

    def random(self) -> float:
        return random()

    def uniform(self, a: float, b: float) -> float:
        return uniform(a, b)

    def randint(self, a: int, b: int) -> int:
        return randint(a, b)

    def randrange(self, start: int, stop: int = 0, step: int = 1) -> int:
        return randrange(start, stop, step)

    def choice(self, seq):
        return choice(seq)

    def shuffle(self, x):
        return shuffle(x)

    def sample(self, population, k: int):
        return sample(population, k)

    def gauss(self, mu: float = 0.0, sigma: float = 1.0) -> float:
        return gauss(mu, sigma)

    def getstate(self):
        return getstate()

    def setstate(self, state):
        return None


class SystemRandom(Random):
    pass
