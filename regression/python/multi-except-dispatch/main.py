# PLR §8.4.4: when multiple except clauses are present, the runtime
# dispatches to the first one whose type matches the raised exception.
# Each handler is a branch — string_constants tracking must be
# invalidated inside each handler body, otherwise the converter
# folds the assertion based on the LAST handler's assignment
# regardless of which one actually runs.

def multi_zero() -> None:
    result: str = ""
    try:
        x = 1 // 0
    except TypeError:
        result = "type"
    except ZeroDivisionError:
        result = "zero"
    assert result == "zero"


def multi_type() -> None:
    result: str = ""
    try:
        raise TypeError("t")
    except TypeError:
        result = "type"
    except ZeroDivisionError:
        result = "zero"
    assert result == "type"


def multi_value() -> None:
    result: str = ""
    try:
        raise ValueError("v")
    except TypeError:
        result = "type"
    except (ValueError, KeyError):
        result = "value-or-key"
    except ZeroDivisionError:
        result = "zero"
    assert result == "value-or-key"


multi_zero()
multi_type()
multi_value()
