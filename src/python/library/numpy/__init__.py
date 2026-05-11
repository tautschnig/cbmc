"""
Verification model of `numpy`.

Numerical computation is not modelled. ndarray is an opaque
shape-bearing container; operations return new nondet
arrays. Shapes, dtypes, and simple element access use
conservative defaults.
"""


# dtype placeholders — numpy uses class objects as dtype
# tokens. Code that compares dtype == numpy.int64 expects
# identity comparison; we use distinct sentinel strings.
int8 = "int8"
int16 = "int16"
int32 = "int32"
int64 = "int64"
uint8 = "uint8"
uint16 = "uint16"
uint32 = "uint32"
uint64 = "uint64"
float16 = "float16"
float32 = "float32"
float64 = "float64"
complex64 = "complex64"
complex128 = "complex128"
bool_ = "bool"
object_ = "object"

nan = float("nan")
inf = float("inf")
pi = 3.141592653589793
e = 2.718281828459045
euler_gamma = 0.5772156649015329


class dtype:
    def __init__(self, t):
        self.name = t if isinstance(t, str) else str(t)

    def __str__(self) -> str:
        return self.name


class ndarray:
    """Opaque N-dimensional array. Constructed via array(),
    zeros(), ones(), etc. Methods return nondet values of
    the expected shape."""

    def __init__(self, shape=None, dtype_=None):
        if shape is None:
            self.shape = (0,)
        elif isinstance(shape, int):
            self.shape = (shape,)
        else:
            self.shape = shape
        self.dtype = dtype_ if dtype_ is not None else float64
        self.ndim = len(self.shape) if hasattr(self.shape, "__len__") else 1
        self.size = 0

    def __len__(self) -> int:
        return self.size

    def __getitem__(self, idx):
        return 0

    def __setitem__(self, idx, value) -> None:
        return None

    def __add__(self, other):
        return ndarray(self.shape, self.dtype)

    def __sub__(self, other):
        return ndarray(self.shape, self.dtype)

    def __mul__(self, other):
        return ndarray(self.shape, self.dtype)

    def __truediv__(self, other):
        return ndarray(self.shape, self.dtype)

    def __matmul__(self, other):
        return ndarray(self.shape, self.dtype)

    def __neg__(self):
        return ndarray(self.shape, self.dtype)

    def reshape(self, *shape):
        return ndarray(shape, self.dtype)

    def flatten(self):
        return ndarray((self.size,), self.dtype)

    def ravel(self):
        return ndarray((self.size,), self.dtype)

    def transpose(self, *axes):
        return ndarray(self.shape, self.dtype)

    @property
    def T(self):
        return ndarray(self.shape, self.dtype)

    def sum(self, axis=None, keepdims=False):
        return 0

    def mean(self, axis=None, keepdims=False):
        return 0.0

    def min(self, axis=None, keepdims=False):
        return 0

    def max(self, axis=None, keepdims=False):
        return 0

    def std(self, axis=None, keepdims=False):
        return 0.0

    def var(self, axis=None, keepdims=False):
        return 0.0

    def argmin(self, axis=None):
        return 0

    def argmax(self, axis=None):
        return 0

    def copy(self):
        return ndarray(self.shape, self.dtype)

    def astype(self, new_dtype):
        return ndarray(self.shape, new_dtype)

    def tolist(self):
        return []

    def item(self):
        return 0

    def fill(self, value) -> None:
        return None

    def all(self, axis=None) -> bool:
        return True

    def any(self, axis=None) -> bool:
        return False


def array(data, dtype_=None):
    return ndarray(None, dtype_)


def asarray(data, dtype_=None):
    return array(data, dtype_)


def zeros(shape, dtype_=None):
    return ndarray(shape, dtype_)


def zeros_like(a, dtype_=None):
    return ndarray(a.shape if hasattr(a, "shape") else None, dtype_)


def ones(shape, dtype_=None):
    return ndarray(shape, dtype_)


def ones_like(a, dtype_=None):
    return ndarray(a.shape if hasattr(a, "shape") else None, dtype_)


def empty(shape, dtype_=None):
    return ndarray(shape, dtype_)


def empty_like(a, dtype_=None):
    return ndarray(a.shape if hasattr(a, "shape") else None, dtype_)


def full(shape, fill_value, dtype_=None):
    return ndarray(shape, dtype_)


def arange(start=0, stop=None, step=1, dtype_=None):
    return ndarray(None, dtype_)


def linspace(start, stop, num=50, endpoint=True, retstep=False, dtype_=None):
    return ndarray((num,), dtype_)


def logspace(start, stop, num=50, endpoint=True, base=10.0, dtype_=None):
    return ndarray((num,), dtype_)


def concatenate(arrays, axis=0):
    return ndarray(None)


def stack(arrays, axis=0):
    return ndarray(None)


def hstack(arrays):
    return ndarray(None)


def vstack(arrays):
    return ndarray(None)


def dot(a, b):
    return ndarray(None)


def matmul(a, b):
    return ndarray(None)


def sum(a, axis=None, keepdims=False):
    return 0


def mean(a, axis=None, keepdims=False):
    return 0.0


def std(a, axis=None, keepdims=False):
    return 0.0


def var(a, axis=None, keepdims=False):
    return 0.0


def min(a, axis=None, keepdims=False):
    return 0


def max(a, axis=None, keepdims=False):
    return 0


def sqrt(x):
    if isinstance(x, ndarray):
        return ndarray(x.shape, x.dtype)
    return 0.0


def exp(x):
    if isinstance(x, ndarray):
        return ndarray(x.shape, x.dtype)
    return 0.0


def log(x):
    if isinstance(x, ndarray):
        return ndarray(x.shape, x.dtype)
    return 0.0


def sin(x):
    if isinstance(x, ndarray):
        return ndarray(x.shape, x.dtype)
    return 0.0


def cos(x):
    if isinstance(x, ndarray):
        return ndarray(x.shape, x.dtype)
    return 0.0


def abs(x):
    if isinstance(x, ndarray):
        return ndarray(x.shape, x.dtype)
    return 0


def where(cond, x=None, y=None):
    return ndarray(None)


def argsort(a, axis=-1):
    return ndarray(None)


def sort(a, axis=-1):
    return ndarray(None)


def unique(ar, return_index=False, return_counts=False):
    return ndarray(None)


def isnan(x):
    return False


def isinf(x):
    return False


def isfinite(x):
    return True


# random submodule placeholder
class _Random:
    def rand(self, *shape):
        return ndarray(shape)

    def randn(self, *shape):
        return ndarray(shape)

    def randint(self, low, high=None, size=None, dtype_=None):
        return ndarray(size) if size is not None else 0

    def random(self, size=None):
        return ndarray(size) if size is not None else 0.0

    def seed(self, s) -> None:
        return None

    def choice(self, a, size=None, replace=True, p=None):
        return ndarray(size) if size is not None else 0

    def normal(self, loc=0.0, scale=1.0, size=None):
        return ndarray(size) if size is not None else 0.0

    def uniform(self, low=0.0, high=1.0, size=None):
        return ndarray(size) if size is not None else 0.0


random = _Random()


# Linear algebra submodule
class _Linalg:
    def inv(self, a):
        return ndarray(None)

    def det(self, a):
        return 0.0

    def solve(self, a, b):
        return ndarray(None)

    def eig(self, a):
        return (ndarray(None), ndarray(None))

    def norm(self, a, ord=None, axis=None):
        return 0.0


linalg = _Linalg()
