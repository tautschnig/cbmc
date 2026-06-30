# PLR: sum() of non-numeric elements -> TypeError (sum starts at 0, 0 + str fails)
s = sum(["a", "b"])
