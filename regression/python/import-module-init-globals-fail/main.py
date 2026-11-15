from shelf import put, count, log_size

put(1, 5)
assert count(1) == 1
assert log_size() == 2
