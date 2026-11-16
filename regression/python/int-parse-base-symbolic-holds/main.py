digits = ['1', '0']
text = digits[0] + digits[1]
assert int(text, 16) == 16
assert int(text, 2) == 2
assert int(text) == 10
