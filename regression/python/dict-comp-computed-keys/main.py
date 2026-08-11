# PLR §6.2.7: dict comprehension where the key is a function of
# the iteration variable (e.g. str(i)). For range-based comps
# whose iteration values are known at conversion time, the key
# is folded to a concrete string literal — both so dict-subscript
# can match by identity, and so dict-equality with a literal works.

def computed_keys() -> None:
    d = {str(i): i * i for i in range(4)}
    assert d['0'] == 0
    assert d['1'] == 1
    assert d['2'] == 4
    assert d['3'] == 9


def dict_equality() -> None:
    d = {str(i): i for i in range(3)}
    assert d == {'0': 0, '1': 1, '2': 2}


def computed_dict_subscript_str_solver() -> None:
    # PLR §6.10.1: dict subscript must use string-content
    # equality for python_string keys, not struct equality
    # (which compares data POINTERS that differ between a
    # literal "0" and a runtime str(0)).
    d: dict = {}
    d['key'] = 42
    assert d['key'] == 42


computed_keys()
dict_equality()
computed_dict_subscript_str_solver()
