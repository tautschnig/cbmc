# PLR §6.3.2: a constant tuple index outside [0, len) raises IndexError. A fixed
# tuple is a struct (_0.._{n-1}); list/str OOB are bounds-checked elsewhere, but
# an OOB constant tuple index fell through to a nondet read (false proof).
def main() -> None:
    t = (1, 2)
    v = t[9]   # IndexError: tuple index out of range


main()
