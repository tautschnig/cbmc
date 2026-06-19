# PLR §6.10.2: range() takes 1..3 positional arguments. Zero args (and
# more than three) raise TypeError, modelled as an uncaught exception.
for i in range():
    pass
