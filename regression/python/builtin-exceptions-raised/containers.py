# Each of these raises (containers and their methods).

hit0 = False
try:
    x0 = set([[]])
except TypeError:
    hit0 = True
assert hit0

hit1 = False
try:
    x1 = frozenset([[1]])
except TypeError:
    hit1 = True
assert hit1

hit2 = False
try:
    x2 = dict([1])
except TypeError:
    hit2 = True
assert hit2

hit3 = False
try:
    x3 = dict([(1, 2, 3)])
except ValueError:
    hit3 = True
assert hit3

hit4 = False
try:
    x4 = {}.popitem()
except KeyError:
    hit4 = True
assert hit4

hit5 = False
try:
    x5 = {}.pop(1)
except KeyError:
    hit5 = True
assert hit5

hit6 = False
try:
    x6 = {1: 2}.pop(3)
except KeyError:
    hit6 = True
assert hit6

hit7 = False
try:
    x7 = [].index()
except TypeError:
    hit7 = True
assert hit7

hit8 = False
try:
    x8 = [1].append()
except TypeError:
    hit8 = True
assert hit8

hit9 = False
try:
    x9 = [1].append(1, 2)
except TypeError:
    hit9 = True
assert hit9

hit10 = False
try:
    x10 = {}.get()
except TypeError:
    hit10 = True
assert hit10

hit11 = False
try:
    x11 = {1}.add()
except TypeError:
    hit11 = True
assert hit11

hit12 = False
try:
    x12 = [1].sort(1)
except TypeError:
    hit12 = True
assert hit12

# and these do not
assert {1: 2}.pop(1) == 2
assert {1: 2}.popitem() == (1, 2)
assert len(set([(1, 2)])) == 1
