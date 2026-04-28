# PLR §6.2.9: Infinite generator — cannot be eagerly evaluated
# Requires lazy evaluation (state machine) to handle correctly
def naturals():
    n: int = 0
    while True:
        yield n
        n += 1

# Take first 5 elements from infinite generator
gen = naturals()
total: int = 0
for i in range(5):
    total += next(gen)
assert total == 10  # 0+1+2+3+4
