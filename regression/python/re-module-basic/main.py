import re
pattern = re.compile("[a-z]+")
m = pattern.search("hello")
assert m == m
