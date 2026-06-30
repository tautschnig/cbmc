# PLR: str.join requires all str elements -> TypeError on a non-str element type
s = ",".join([1, 2, 3])
