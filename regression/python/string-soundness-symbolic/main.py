# Soundness (P1 audit lock-in): string operations on a symbolic string must not
# false-prove. Each assertion is genuinely FALSE, so verification must FAIL
# (none may be proved on the refined-string default backend).
s = input()
t = s + "x"
assert len(t) == len(s)         # produced-string length must grow
assert t == s                   # concat result is not the original
assert s.upper() == "ABC"       # symbolic upper is not a fixed value
assert s.count("a") == 0        # symbolic count is not fixed 0
