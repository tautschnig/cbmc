class Thing:
    pass

# A name used first as an integer and then rebound to a class
# instance. The front-end used to crash when trying to set
# __class_tag on a symbol whose existing type was scalar.
x = 1
x = Thing()
assert True
