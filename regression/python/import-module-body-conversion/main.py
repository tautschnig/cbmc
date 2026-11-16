from gate_mod import *

d = {'src': ['MEM'], 'lane': [3], 'w': ['0x0000000000001800']}
ok = True
try:
    check(d)
except AssertionError:
    ok = False
assert ok == False
