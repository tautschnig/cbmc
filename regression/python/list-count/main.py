# PLR §6.4.6: list.count(v) returns the number of occurrences. Was unmodelled
# (nondet) -> false alarms. Now a symbolic sum over matching in-range elements
# (precise for constant AND symbolic lists, and after mutation).
assert [1, 2, 2, 3].count(2) == 2
assert [1, 2, 3].count(9) == 0
assert [5, 5, 5].count(5) == 3
xs = [1, 2]
xs.append(2)
assert xs.count(2) == 2
