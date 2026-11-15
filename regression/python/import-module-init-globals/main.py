from shelf import put, count, inside, log_size

put(1, 5)
put(1, 6)
assert count(1) == 2
assert count(0) == 0
assert inside(0x4f)
assert not inside(0x50)
assert log_size() == 1
