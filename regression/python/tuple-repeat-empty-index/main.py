# `t * 0` is the empty tuple; .index() on it raises ValueError (was missed
# because t*0 kept its elements -- a false proof).
t = (1, 2)
t = t * 0
r = t.index(1)
