def f(d: dict[str, int]) -> None:
    # Adding a key while iterating a dict view raises
    # RuntimeError: dictionary changed size during iteration (CPython).
    # We detect it by snapshotting len(d) at loop entry and raising when the
    # size changes at a __next__ point. (github_3647_9)
    for k, v in d.items():
        d["x"] = 3


f({"a": 1})
