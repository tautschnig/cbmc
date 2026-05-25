# PLR §3.2: bool is a subtype of int; both promote to float
# when used as complex() arguments. Previously the complex()
# constructor only handled raw int constants — UnaryOp
# (e.g. -1) and Bool constants fell through to safe_zero,
# producing complex(0, 0) for any non-direct-int input.

def complex_negative_int() -> None:
    z = complex(-1, -2)
    assert z.real == -1.0
    assert z.imag == -2.0


def complex_bool_args() -> None:
    z = complex(True, False)
    assert z.real == 1.0
    assert z.imag == 0.0


def complex_zero_args() -> None:
    z = complex(0, 0)
    assert z.real == 0.0
    assert z.imag == 0.0


def complex_int_args() -> None:
    z = complex(3, 4)
    assert z.real == 3.0
    assert z.imag == 4.0


complex_negative_int()
complex_bool_args()
complex_zero_args()
complex_int_args()
