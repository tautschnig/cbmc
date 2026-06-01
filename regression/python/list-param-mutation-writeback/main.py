# PLR §3.1: mutable containers are passed by reference. A
# function that mutates a list parameter (append or element
# assignment) must have the mutation visible to the caller —
# even when the parameter's element type differs from the
# caller's concrete list (a bare `list` annotation widens to
# list[python_value], forcing a promoted by-ref copy whose
# mutations are written back after the call).


def append_one(lst: list) -> None:
    lst.append(99)


def set_first(lst: list) -> None:
    lst[0] = 42


a = [1, 2]
append_one(a)
assert len(a) == 3
assert a[2] == 99

b = [7, 8]
set_first(b)
assert b[0] == 42
assert b[1] == 8
