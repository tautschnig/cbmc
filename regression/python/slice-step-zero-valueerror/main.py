# PLR §6.3.3: a slice with step 0 raises ValueError.
xs = [1, 2, 3]
y = xs[::0]
