# PLR 6.1.3: a str.format presentation code incompatible with the arg type
# raises ValueError ({:d} on a str).
s = "{:d}".format("hello")
