# KNOWNBUG (differential audit r2): a %-format / format-spec type mismatch
# ("%d" % "x", "{:d}".format("hello"), f"{s:d}") -> CPython TypeError/ValueError;
# format-spec/type validation is not modelled. Representative of the format
# cluster. Desired: VERIFICATION FAILED.
s = "%d" % "x"
