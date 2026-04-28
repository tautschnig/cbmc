# PLR §6.10.2: isinstance negative — not isinstance(42, str)
i: int = 42
assert not isinstance(i, str)
