# PEP 654 multi-element ExceptionGroup.
# 'raise ExceptionGroup(msg, [E1, E2, ...])' is
# over-approximated by non-det picking any of the N
# element types and setting __exception_type to its
# hash. Symex then explores both paths via except*.

def classify() -> int:
    try:
        raise ExceptionGroup("group", [ValueError("v"), TypeError("t")])
    except* ValueError:
        return 1
    except* TypeError:
        return 2
    return 0


r = classify()
# r is non-det 1 or 2 — both paths are valid.
assert r == 1 or r == 2
