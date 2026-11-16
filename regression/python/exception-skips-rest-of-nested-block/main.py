log = []
try:
    for i in range(3):
        if i == 1:
            raise ValueError()
        log.append(i)
except ValueError:
    pass
assert len(log) != 1      # false in CPython: log == [0]
