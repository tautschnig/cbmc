# PLR §6.2.5 + §6.10.2: aggregate builtins (sum, min, max) over
# *args. The args parameter has list[python_value] type because
# *args is packed at the call site as a tagged-union list. The
# aggregates need to unwrap each element to its numeric content
# rather than apply arithmetic on the python_value struct.
#
# Two fixes:
#   - sum(): unwrap python_value to int (or accumulator type)
#     and use that as the accumulator type.
#   - min/max(): when the list is non-literal but its element
#     type is python_value or numeric, emit a runtime reduction
#     loop that walks data[0..length-1] with a guarded update.

def sum_args() -> None:
    def f(*args):
        return sum(args)
    assert f(1, 2, 3) == 6
    assert f(10, 20, 30) == 60


def min_args() -> None:
    def f(*args):
        return min(args)
    assert f(3, 1, 2) == 1
    assert f(5, 5, 5) == 5


def max_args() -> None:
    def f(*args):
        return max(args)
    assert f(3, 5, 2) == 5
    assert f(0, 0, 0) == 0


def len_args() -> None:
    def f(*args):
        return len(args)
    assert f() == 0
    assert f(1, 2, 3) == 3


sum_args()
min_args()
max_args()
len_args()
