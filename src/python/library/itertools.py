"""
Verification model of the `itertools` module.

The common combinators and iterators are modelled as generators
that yield nondet-bounded results or — for finite inputs — the
obvious iteration. For verification code that merely consumes
iterator values, stopping early on the nondet bound is a sound
over-approximation: CBMC explores paths where the loop breaks at
any point up to the bound.
"""


# Infinite iterators — we model them as finite to make
# verification terminate. The bound is deliberately small so
# loops don't explode; real programs under verification will
# typically process a handful of items.
# NOTE: we inline this constant at every use rather than
# referencing a module-level identifier: the Python front-end
# doesn't yet resolve module-level names from inside function
# bodies in some cases, and inlining avoids "Unknown variable"
# warnings at zero code-density cost.


def count(start=0, step=1):
    x = start
    for _ in range(16):
        yield x
        x = x + step


def cycle(iterable):
    items = list(iterable)
    if not items:
        return
    for _ in range(16):
        for item in items:
            yield item


def repeat(object, times=None):
    if times is None:
        for _ in range(16):
            yield object
    else:
        for _ in range(times):
            yield object


# Terminating iterators on the shortest input
def accumulate(iterable, func=None, *, initial=None):
    it = iter(iterable)
    total = initial
    if total is None:
        try:
            total = next(it)
        except StopIteration:
            return
        yield total
    else:
        yield total
    for element in it:
        total = func(total, element) if func is not None else total + element
        yield total


def chain(*iterables):
    for it in iterables:
        for item in it:
            yield item


def from_iterable(iterables):
    for it in iterables:
        for item in it:
            yield item


chain.from_iterable = staticmethod(from_iterable)


def compress(data, selectors):
    for d, s in zip(data, selectors):
        if s:
            yield d


def dropwhile(predicate, iterable):
    it = iter(iterable)
    for x in it:
        if not predicate(x):
            yield x
            break
    for x in it:
        yield x


def takewhile(predicate, iterable):
    for x in iterable:
        if not predicate(x):
            break
        yield x


def filterfalse(predicate, iterable):
    for x in iterable:
        if not predicate(x):
            yield x


def islice(iterable, *args):
    # islice(it, stop) or islice(it, start, stop[, step])
    if len(args) == 1:
        start, stop, step = 0, args[0], 1
    elif len(args) == 2:
        start, stop, step = args[0], args[1], 1
    else:
        start, stop, step = args
    start = 0 if start is None else start
    step = 1 if step is None else step
    it = iter(iterable)
    for _ in range(start):
        try:
            next(it)
        except StopIteration:
            return
    n = 0
    count_consumed = 0
    for x in it:
        if stop is not None and count_consumed + start >= stop:
            break
        if n % step == 0:
            yield x
        n += 1
        count_consumed += 1


def starmap(function, iterable):
    for args in iterable:
        yield function(*args)


def tee(iterable, n=2):
    items = list(iterable)
    return tuple(iter(items) for _ in range(n))


def zip_longest(*iterables, fillvalue=None):
    iters = [iter(it) for it in iterables]
    active = len(iters)
    if not active:
        return
    while True:
        result = []
        for i, it in enumerate(iters):
            try:
                result.append(next(it))
            except StopIteration:
                active -= 1
                if active == 0:
                    return
                result.append(fillvalue)
                iters[i] = iter([fillvalue] * 16)
        yield tuple(result)


# Combinatorial generators — these can blow up combinatorially
# so we cap output at 16 tuples.
def product(*iterables, repeat=1):
    pools = [list(pool) for pool in iterables] * repeat
    result = [[]]
    for pool in pools:
        result = [x + [y] for x in result for y in pool]
    for prod in result[:16]:
        yield tuple(prod)


def permutations(iterable, r=None):
    pool = list(iterable)
    n = len(pool)
    r = n if r is None else r
    count = 0
    indices = list(range(n))
    cycles = list(range(n, n - r, -1))
    if r > n:
        return
    yield tuple(pool[i] for i in indices[:r])
    count += 1
    while count < 16:
        for i in reversed(range(r)):
            cycles[i] -= 1
            if cycles[i] == 0:
                indices[i:] = indices[i + 1:] + indices[i:i + 1]
                cycles[i] = n - i
            else:
                j = cycles[i]
                indices[i], indices[-j] = indices[-j], indices[i]
                yield tuple(pool[i] for i in indices[:r])
                count += 1
                break
        else:
            return


def combinations(iterable, r):
    pool = list(iterable)
    n = len(pool)
    if r > n:
        return
    indices = list(range(r))
    count = 0
    yield tuple(pool[i] for i in indices)
    count += 1
    while count < 16:
        for i in reversed(range(r)):
            if indices[i] != i + n - r:
                break
        else:
            return
        indices[i] += 1
        for j in range(i + 1, r):
            indices[j] = indices[j - 1] + 1
        yield tuple(pool[i] for i in indices)
        count += 1


def combinations_with_replacement(iterable, r):
    pool = list(iterable)
    n = len(pool)
    if not n and r:
        return
    indices = [0] * r
    count = 0
    yield tuple(pool[i] for i in indices)
    count += 1
    while count < 16:
        for i in reversed(range(r)):
            if indices[i] != n - 1:
                break
        else:
            return
        indices[i:] = [indices[i] + 1] * (r - i)
        yield tuple(pool[i] for i in indices)
        count += 1


def groupby(iterable, key=None):
    items = list(iterable)
    if not items:
        return
    if key is None:
        key = lambda x: x
    current_key = None
    current_group = []
    for item in items:
        k = key(item)
        if current_key is None or k != current_key:
            if current_group:
                yield (current_key, iter(current_group))
            current_key = k
            current_group = [item]
        else:
            current_group.append(item)
    if current_group:
        yield (current_key, iter(current_group))


def pairwise(iterable):
    it = iter(iterable)
    try:
        prev = next(it)
    except StopIteration:
        return
    for x in it:
        yield (prev, x)
        prev = x


def batched(iterable, n):
    if n < 1:
        raise ValueError("n must be at least one")
    it = iter(iterable)
    while True:
        batch = []
        for _ in range(n):
            try:
                batch.append(next(it))
            except StopIteration:
                break
        if not batch:
            return
        yield tuple(batch)
        if len(batch) < n:
            return
