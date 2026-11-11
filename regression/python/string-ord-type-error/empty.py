# ord("") raises an uncaught TypeError.
s = nondet_str()
if s == "":
    n = ord(s)
