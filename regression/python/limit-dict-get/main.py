# PLR §4.10: dict.get() — not yet modeled, returns nondet
d = {"a": 1, "b": 2}
x = d.get("a")
# Can't assert value (get returns nondet), but no crash
