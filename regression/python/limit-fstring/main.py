# PLR §2.4.3: formatted string literals
# f-strings are modeled as nondet strings (sound overapproximation)
x: int = 42
s: str = f"value is {x}"
