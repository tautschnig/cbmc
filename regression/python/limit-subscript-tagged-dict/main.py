def get_nested(d):
    return d["outer"]["inner"]

result = get_nested({"outer": {"inner": 42}})
assert result == 42
