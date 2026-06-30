# PLR 6.10.1: sorting a list whose constant elements span numeric and str
# categories raises TypeError (the sort compares a number with a str).
xs = sorted([1, "a"])
