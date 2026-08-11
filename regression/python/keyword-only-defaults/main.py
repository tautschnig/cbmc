# PLR §8.7: keyword-only parameter defaults are stored in
# kw_defaults (not the regular `defaults` array). Each entry is
# either an expression (default value) or null (required).
# convert_function_def now records these in default_values
# alongside positional defaults, so the call site can fill
# them in just like ordinary defaults.

def with_default(a: int, *, b: int = 10) -> int:
    return a + b


def with_explicit(a: int, *, b: int = 10) -> int:
    return a + b


# default applied
assert with_default(1) == 11
assert with_default(5) == 15

# explicit override
assert with_explicit(1, b=2) == 3
assert with_explicit(5, b=20) == 25
