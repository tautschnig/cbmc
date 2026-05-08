# PLR §6.2.7: Dictionary comprehensions.
#
# Exercises the new convert_dict_comp implementation:
#   * range() iterable
#   * literal list iterable
#   * filter 'if' clauses
#   * string keys
#   * assertion on both membership and value

# range() iterable, int key.
squares = {k: k * k for k in range(4)}
assert squares[0] == 0
assert squares[1] == 1
assert squares[2] == 4
assert squares[3] == 9


# literal-list iterable, int key.
doubled = {k: k * 2 for k in [5, 6, 7]}
assert doubled[5] == 10
assert doubled[6] == 12
assert doubled[7] == 14


# Filter 'if' clause.
evens = {k: k * 10 for k in range(5) if k % 2 == 0}
assert evens[0] == 0
assert evens[2] == 20
assert evens[4] == 40


# String keys with constant literal values.
flags = {k: True for k in ['alpha', 'beta']}
assert flags['alpha'] == True
assert flags['beta'] == True
