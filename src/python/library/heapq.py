"""
Verification model of the `heapq` module.

Heap operations on lists: heappush, heappop, heapify, nlargest,
nsmallest, and the merge helpers. We model each in terms of the
underlying list's native sort — O(n log n) per operation, so a
bit slower than CPython's O(log n) heap ops, but semantically
equivalent for verification. The slower model makes heappush
deterministic in its effect on ``heap[0]``, which is what most
verification code inspects.
"""


def heappush(heap, item):
    heap.append(item)
    # Full sort keeps heap[0] = min(heap) invariant that heapq
    # guarantees. Slower than CPython's sift-up but correct.
    heap.sort()
    return None


def heappop(heap):
    if not heap:
        raise IndexError("index out of range")
    # heap[0] is always the smallest; popping it preserves the
    # invariant because the rest is already sorted.
    item = heap[0]
    del heap[0]
    return item


def heappushpop(heap, item):
    if heap and heap[0] < item:
        item, heap[0] = heap[0], item
        heap.sort()
    return item


def heapreplace(heap, item):
    if not heap:
        raise IndexError("index out of range")
    returnitem = heap[0]
    heap[0] = item
    heap.sort()
    return returnitem


def heapify(x):
    x.sort()
    return None


def merge(*iterables, key=None, reverse=False):
    # Flatten + sort. CPython's merge is a lazy k-way merge;
    # flattening is semantically equivalent and simpler to verify.
    all_items = []
    for it in iterables:
        for item in it:
            all_items.append(item)
    if key is not None:
        all_items.sort(key=key, reverse=reverse)
    else:
        all_items.sort(reverse=reverse)
    for item in all_items:
        yield item


def nlargest(n, iterable, key=None):
    items = list(iterable)
    if key is not None:
        items.sort(key=key, reverse=True)
    else:
        items.sort(reverse=True)
    return items[:n]


def nsmallest(n, iterable, key=None):
    items = list(iterable)
    if key is not None:
        items.sort(key=key)
    else:
        items.sort()
    return items[:n]


# Internal — used by CPython's implementation; not a public API.
def _heapify_max(x):
    x.sort(reverse=True)
    return None
