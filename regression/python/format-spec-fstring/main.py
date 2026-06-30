# PLR 2.4.3: an f-string format presentation code incompatible with the value's
# type raises ValueError (a str value with the integer code :d).
x = "hi"
s = f"{x:d}"
