# PLR §3.3.1: a non-iterable scalar (int/float/bool) is not iterable -- `for x in
# 5` raises TypeError. Only bare numeric scalar types are flagged; str/list/dict/
# set/tuple/range/class/Any are legitimately iterable.
for i in 5:
    pass
