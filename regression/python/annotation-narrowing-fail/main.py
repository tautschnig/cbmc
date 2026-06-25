# --python-check-annotations (P3): a float is NOT assignable where an int is
# declared (narrowing loses the fraction); flagged across argument, return, and
# assignment boundaries by the shared annotation_types_incompatible helper.
# (int->float widening stays compatible — covered by annotation-widening-ok.)
def process(value: int) -> None:
    pass

process(3.14)
