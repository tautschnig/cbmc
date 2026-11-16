digits = ['1', '0']
text = digits[0] + digits[1]
value = int(text, 16)
assert value == 10        # false: int('10', 16) == 16
