# PLR §6.7: floor division on complex should raise TypeError
z = complex(1, 2)
caught: bool = False
try:
    result = z // 2
except TypeError:
    caught = True
assert caught
