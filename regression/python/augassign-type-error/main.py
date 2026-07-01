# PLR §7.2.2 / §6.7: augmented assignment applies the binary operator, so
# `x += "a"` for an int x is `int + str` -> TypeError. convert_aug_assign now
# reuses the shared binop_operand_type_error check (the same operand-type
# obligation the plain `+` path uses). CPython: TypeError; expected: FAILED.
x = 1
x += "a"
