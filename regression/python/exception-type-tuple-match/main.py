# Exception-handler type matching: ensure `except (A, B):` only
# catches A or B — not every exception. The single-name form
# already worked correctly (via exception_type_hash); this test
# covers the tuple form which previously fell through to a
# catch-all.


def raise_runtime():
    raise RuntimeError("boom")


def raise_value():
    raise ValueError("vbad")


# Case 1: except (ValueError, TypeError) must NOT catch a
# RuntimeError.
caught_wrong = False
try:
    try:
        raise_runtime()
    except (ValueError, TypeError):
        caught_wrong = True
except RuntimeError:
    pass

assert not caught_wrong


# Case 2: except (ValueError, TypeError) SHOULD catch a
# ValueError.
caught_right = False
try:
    raise_value()
except (ValueError, TypeError):
    caught_right = True

assert caught_right


# Case 3: except Exception catches everything (catch-all keeps
# working as before).
caught_any = False
try:
    raise_runtime()
except Exception:
    caught_any = True

assert caught_any
