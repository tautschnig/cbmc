# IndexError stays exact under the flag: reading past length raises,
# independent of the (now infinite) data array.
xs = [1, 2, 3]
y = xs[5]
