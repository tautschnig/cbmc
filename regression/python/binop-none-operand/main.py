# PLR §6.7: None supports no arithmetic/bitwise operator; `None * 1` raises
# TypeError. compute_binop_verdict flags a provable None operand. Expected:
# VERIFICATION FAILED.
r = None * 1
