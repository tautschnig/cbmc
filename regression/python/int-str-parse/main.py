# PLR builtins: int(str) — parses integer from string.
# Constants fold at parse time; symbolic strings route
# through cprover_string_parse_int_func.


# Constant strings fold.
assert int("42") == 42
assert int("-7") == -7
assert int("0") == 0

# Symbolic string path emits the intrinsic.
def convert(s: str) -> int:
    return int(s)


x = convert("123")
# The intrinsic result is nondet in the symbolic case;
# just verify it produces a usable int.
y = x * 2
