# A loop bound parsed from a constant numeral is a constant.
width = int("1c", 16)
total = 0
for k in range(width):
    total = total + 1
assert total == 28
assert int("-0012") == -12 and int("+7") == 7
