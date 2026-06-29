# --python-check-annotations precision: the element/value-store checks fire only
# for EXPLICITLY-annotated containers (in variable_annotations), not inferred
# ones. An inferred empty `[]` (here from `setdefault`) gets a default element
# type that need not reflect the real contents, so appending into it must NOT be
# flagged (dict_setdefault_list false positive).
def test() -> None:
    a = {}
    a.setdefault(1, []).append(1.0)
    a.setdefault(1, []).append(2.0)
    assert len(a.setdefault(1, [])) == 2


test()
