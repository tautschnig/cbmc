# PLR 6.5: printf-style %-format with a conversion incompatible with the arg's
# type raises TypeError (%d on a str).
s = "%d" % "x"
