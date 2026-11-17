# Each assertion fails in CPython. Refined case conversion of a string with
# a character beyond ASCII required a longer result (as for the Java
# characters whose conversion grows), so these verified.
assert "a\u00e9".lower() != "a\u00e9", "lower"
assert "\u00e9".upper() != "\u00c9", "upper"
