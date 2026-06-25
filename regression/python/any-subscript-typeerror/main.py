# differential: subscripting a non-subscriptable value raises TypeError.
# An unannotated parameter inferred as int (called first(5)) then evaluating
# xs[0] must raise TypeError ('int' object is not subscriptable). The subscript
# tag obligation (concrete-scalar receiver = definite TypeError; python_value
# receiver = tag must be STR/LIST/DICT/CLASS) now detects this.
def first(xs):
    return xs[0]

first(5)
