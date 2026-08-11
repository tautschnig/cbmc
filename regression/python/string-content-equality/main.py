# PLR §6.10.1: string equality must compare CONTENT, not the
# data-pointer struct. The same fix that the prior commit applied
# to dict subscript also applies to:
#   - the 'in' operator on lists of strings
#   - list comprehension elements that construct strings
# (and prepares the ground for set() membership on string-keyed
# sets; the set-of-strings model is list-backed, see set() empty-
# constructor fix.)

def list_in_constructed_strings() -> None:
    xs = [str(i) for i in range(3)]
    assert "0" in xs
    assert "1" in xs
    assert "2" in xs


def list_in_literal_strings() -> None:
    xs = ["a", "b", "c"]
    assert "b" in xs
    assert "x" not in xs


def empty_set_construct() -> None:
    s = set()
    # Empty set is a struct (length=0, data=zeros), not nondet.
    assert len(s) == 0


list_in_constructed_strings()
list_in_literal_strings()
empty_set_construct()
