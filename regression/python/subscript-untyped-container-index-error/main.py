def walk(t):
    s = 0
    for j in range(3):
        s = s + t['k'][j]
    return s


walk({'k': [4, 5], 'm': [1, 2], 'z': [0, 0]})
