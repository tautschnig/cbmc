# Absent value in a symbolic-element tuple raises ValueError (was missed).
b = 3
a = 1
t = (b, a)
r = t.index(9)
