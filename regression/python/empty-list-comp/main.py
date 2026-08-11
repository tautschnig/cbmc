# PLR §6.2.4: empty list comprehensions yield an empty list,
# not nil. Returning nil_exprt at conversion time caused the
# surrounding assignment / equality to be silently dropped,
# masking incorrect assertions. Fix: return an empty
# python_list_type struct (length=0, data zero-init).

def empty_listcomp_eq() -> None:
    xs = [x for x in []]
    assert xs == []


def empty_listcomp_len() -> None:
    xs = [x for x in []]
    assert len(xs) == 0


def empty_listcomp_filter() -> None:
    xs = [x for x in [1, 2, 3] if x > 100]
    assert xs == []


empty_listcomp_eq()
empty_listcomp_len()
empty_listcomp_filter()
