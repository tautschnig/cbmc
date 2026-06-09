# PLR §6.10.1: complex() argument validation. Unknown keywords, a keyword
# duplicating a positional argument, and bytes/bytearray arguments all
# raise TypeError; valid positional and real/imag keyword forms still work.
def t() -> None:
    r = False
    try:
        complex(foo=1)
    except TypeError:
        r = True
    assert r

    r = False
    try:
        complex(1, real=2)
    except TypeError:
        r = True
    assert r

    r = False
    try:
        complex(1, 2, imag=3)
    except TypeError:
        r = True
    assert r

    r = False
    try:
        complex(b"7+8j")
    except TypeError:
        r = True
    assert r

    r = False
    try:
        complex(bytearray(b"-3.5"))
    except TypeError:
        r = True
    assert r

    # valid forms unaffected
    z = complex(real=2, imag=3)
    assert z.real == 2.0 and z.imag == 3.0
    w = complex(1, 2)
    assert w.real == 1.0 and w.imag == 2.0


t()
