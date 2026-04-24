# Python Language Reference §8.3: for statement
# Iterating over a dict yields its keys
d = {"a": 1, "b": 2}
keys = []
for k in d:
    keys.append(k)
assert len(keys) == 2
