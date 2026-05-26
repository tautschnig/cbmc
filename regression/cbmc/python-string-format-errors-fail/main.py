# Companion: '{} {}'.format('a') has only 1 arg for 2
# placeholders, so format() raises IndexError, the program
# exits with an uncaught exception, and verification fails.
"{} {}".format("a")
