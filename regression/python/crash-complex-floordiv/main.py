# PLR §6.7: floor division on complex should raise TypeError
z = complex(1, 2)
try:
    result = z // 2
    assert False
except TypeError:
    pass
