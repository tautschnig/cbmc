# PLR §7.5: 'del d[key]' raises KeyError when the key is not
# present in the dict. Prior to this fix the converter
# silently succeeded on missing keys, so try/except KeyError
# never triggered and standalone bugs went undetected.

def del_missing_caught() -> None:
    d = {}
    try:
        del d["x"]
        assert False, "should have raised KeyError"
    except KeyError:
        assert True


def del_present_no_raise() -> None:
    d = {1: "one", 2: "two"}
    try:
        del d[1]
    except KeyError:
        assert False, "1 was present, no raise"
    assert 1 not in d
    assert 2 in d


def del_then_check() -> None:
    d = {"a": 1, "b": 2, "c": 3}
    del d["b"]
    assert "b" not in d
    assert "a" in d
    assert "c" in d


del_missing_caught()
del_present_no_raise()
del_then_check()
