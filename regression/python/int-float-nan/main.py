# PLR §6.10: int() of a floating-point NaN raises ValueError.
x = int(float("nan"))
