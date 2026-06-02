# PLR §6.10.1: min()/max() order their arguments, so mixing a number
# and a string raises TypeError.
m = min(1, "a")
