# PLR §6.10.1: an ordered comparison between a number and a string
# raises TypeError (unlike ==, which is just False).
x = 1
s = "a"
b = x < s
