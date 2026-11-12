# Each of these raises; the exception was not raised, or was a failing
# property that no handler could catch.
import math

hit0 = False
try:
    x0 = abs("a")
except TypeError:
    hit0 = True
assert hit0

hit1 = False
try:
    x1 = {1}.remove(5)
except KeyError:
    hit1 = True
assert hit1

hit2 = False
try:
    x2 = math.floor(float("nan"))
except ValueError:
    hit2 = True
assert hit2

hit3 = False
try:
    x3 = math.ceil(float("inf"))
except OverflowError:
    hit3 = True
assert hit3

hit4 = False
try:
    x4 = list(5)
except TypeError:
    hit4 = True
assert hit4

hit5 = False
try:
    x5 = tuple(1)
except TypeError:
    hit5 = True
assert hit5

hit6 = False
try:
    x6 = set(1.5)
except TypeError:
    hit6 = True
assert hit6

hit7 = False
try:
    x7 = sorted(None)
except TypeError:
    hit7 = True
assert hit7

hit8 = False
try:
    x8 = sum(5)
except TypeError:
    hit8 = True
assert hit8

hit9 = False
try:
    x9 = enumerate(5)
except TypeError:
    hit9 = True
assert hit9

hit10 = False
try:
    x10 = zip([1], 5)
except TypeError:
    hit10 = True
assert hit10

hit11 = False
try:
    x11 = any(5)
except TypeError:
    hit11 = True
assert hit11

hit12 = False
try:
    x12 = min(5)
except TypeError:
    hit12 = True
assert hit12

hit13 = False
try:
    x13 = hash([])
except TypeError:
    hit13 = True
assert hit13

hit14 = False
try:
    x14 = hash((1, [2]))
except TypeError:
    hit14 = True
assert hit14

hit15 = False
try:
    x15 = int(None)
except TypeError:
    hit15 = True
assert hit15

hit16 = False
try:
    x16 = float(None)
except TypeError:
    hit16 = True
assert hit16

hit17 = False
try:
    x17 = len()
except TypeError:
    hit17 = True
assert hit17

hit18 = False
try:
    x18 = len([], [])
except TypeError:
    hit18 = True
assert hit18

hit19 = False
try:
    x19 = abs(1, 2)
except TypeError:
    hit19 = True
assert hit19

hit20 = False
try:
    x20 = int("1", 2, 3)
except TypeError:
    hit20 = True
assert hit20

hit21 = False
try:
    x21 = "a".upper(1)
except TypeError:
    hit21 = True
assert hit21

hit22 = False
try:
    x22 = "a".find()
except TypeError:
    hit22 = True
assert hit22

hit23 = False
try:
    x23 = "".join(5)
except TypeError:
    hit23 = True
assert hit23

hit24 = False
try:
    x24 = max([])
except ValueError:
    hit24 = True
assert hit24
