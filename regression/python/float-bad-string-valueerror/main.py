# PLR §6.4.4: float() of a string it cannot parse raises ValueError
# (mirrors int()'s invalid-literal check).
x = float("xyz")
