# PLR §6.11: truth value testing
# None in boolean context should be falsy
x = None
assert not any([x])
