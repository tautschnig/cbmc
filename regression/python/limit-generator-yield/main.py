# PLR §6.2.9: Generator expressions / yield
def count_up(n: int):
    i: int = 0
    while i < n:
        yield i
        i += 1

total: int = 0
for x in count_up(5):
    total += x
assert total == 10  # 0+1+2+3+4
