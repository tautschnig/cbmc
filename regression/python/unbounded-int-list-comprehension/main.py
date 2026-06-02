# Under --python-unbounded-ints (integer_typet), list literals,
# indexing, and comprehensions over range() used to mis-type the
# list length/array-dimension (signedbv vs integer) and drop range
# comprehensions (range args not recognised as integer_typet),
# causing SSA-assignment invariant crashes and wrong lengths.
xs = [10, 20, 30]
assert xs[1] == 20
assert len(xs) == 3

ys = [i for i in range(10)]
assert len(ys) == 10
assert ys[7] == 7

zs = [i * 2 for i in range(5)]
assert len(zs) == 5
assert zs[3] == 6
