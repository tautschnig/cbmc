"""
Verification model of `pandas`.

Tabular data manipulation is not modelled. DataFrame,
Series, and Index are opaque carriers with the common
attribute/method surface; numeric and I/O operations
return nondet values with conservative defaults.
"""


NA = None
NaT = None


class Series:
    def __init__(self, data=None, index=None, dtype=None, name=None,
                 copy=False):
        self._data = data if data is not None else []
        self.name = name
        self.dtype = dtype
        self.size = 0

    def __len__(self) -> int:
        return self.size

    def __getitem__(self, key):
        return 0

    def __setitem__(self, key, value) -> None:
        return None

    def __iter__(self):
        return iter([])

    def __add__(self, other):
        return Series()

    def __sub__(self, other):
        return Series()

    def __mul__(self, other):
        return Series()

    def __truediv__(self, other):
        return Series()

    def sum(self, axis=None, skipna=True):
        return 0

    def mean(self, axis=None, skipna=True):
        return 0.0

    def min(self, axis=None, skipna=True):
        return 0

    def max(self, axis=None, skipna=True):
        return 0

    def std(self, axis=None, skipna=True):
        return 0.0

    def var(self, axis=None, skipna=True):
        return 0.0

    def count(self):
        return 0

    def value_counts(self, normalize=False, sort=True, ascending=False):
        return Series()

    def unique(self):
        return []

    def nunique(self, dropna=True):
        return 0

    def isna(self):
        return Series()

    def notna(self):
        return Series()

    def dropna(self):
        return Series()

    def fillna(self, value=None, method=None):
        return Series()

    def astype(self, dtype):
        return Series()

    def apply(self, func, convert_dtype=True):
        return Series()

    def map(self, arg):
        return Series()

    def sort_values(self, ascending=True):
        return Series()

    def reset_index(self, drop=False):
        return Series()

    def head(self, n: int = 5):
        return Series()

    def tail(self, n: int = 5):
        return Series()

    def to_list(self):
        return []

    def to_dict(self):
        return {}

    def tolist(self):
        return []

    def copy(self, deep=True):
        return Series()

    @property
    def values(self):
        return []

    @property
    def index(self):
        return Index()

    @property
    def iloc(self):
        return _Accessor()

    @property
    def loc(self):
        return _Accessor()

    @property
    def str(self):
        return _StrAccessor()


class DataFrame:
    def __init__(self, data=None, index=None, columns=None, dtype=None,
                 copy=False):
        self._data = data
        self.columns = columns if columns is not None else []
        self.shape = (0, 0)

    def __len__(self) -> int:
        return 0

    def __getitem__(self, key):
        if isinstance(key, list):
            return DataFrame()
        return Series()

    def __setitem__(self, key, value) -> None:
        return None

    def __iter__(self):
        return iter([])

    def head(self, n: int = 5):
        return DataFrame()

    def tail(self, n: int = 5):
        return DataFrame()

    def info(self, verbose=None) -> None:
        return None

    def describe(self, percentiles=None, include=None, exclude=None):
        return DataFrame()

    def drop(self, labels=None, axis=0, columns=None, inplace=False):
        return DataFrame()

    def dropna(self, axis=0, how="any", subset=None, inplace=False):
        return DataFrame()

    def fillna(self, value=None, method=None, inplace=False):
        return DataFrame()

    def rename(self, mapper=None, index=None, columns=None, inplace=False):
        return DataFrame()

    def sort_values(self, by=None, axis=0, ascending=True, inplace=False):
        return DataFrame()

    def sort_index(self, axis=0, ascending=True, inplace=False):
        return DataFrame()

    def reset_index(self, drop=False, inplace=False):
        return DataFrame()

    def set_index(self, keys, drop=True, inplace=False):
        return DataFrame()

    def merge(self, right, how="inner", on=None, left_on=None, right_on=None):
        return DataFrame()

    def join(self, other, on=None, how="left", lsuffix="", rsuffix=""):
        return DataFrame()

    def concat(self, other, axis=0):
        return DataFrame()

    def groupby(self, by=None, axis=0, level=None, as_index=True):
        return _GroupBy()

    def apply(self, func, axis=0):
        return DataFrame()

    def applymap(self, func):
        return DataFrame()

    def pivot(self, index=None, columns=None, values=None):
        return DataFrame()

    def pivot_table(self, values=None, index=None, columns=None,
                    aggfunc="mean"):
        return DataFrame()

    def sum(self, axis=0, skipna=True):
        return Series()

    def mean(self, axis=0, skipna=True):
        return Series()

    def min(self, axis=0, skipna=True):
        return Series()

    def max(self, axis=0, skipna=True):
        return Series()

    def std(self, axis=0, skipna=True):
        return Series()

    def count(self, axis=0):
        return Series()

    def copy(self, deep=True):
        return DataFrame()

    def to_csv(self, path_or_buf=None, sep=",", index=True):
        return ""

    def to_dict(self, orient="dict"):
        return {}

    def to_numpy(self):
        return []

    def iterrows(self):
        return iter([])

    def itertuples(self, index=True, name="Pandas"):
        return iter([])

    def isna(self):
        return DataFrame()

    def notna(self):
        return DataFrame()

    def duplicated(self, subset=None, keep="first"):
        return Series()

    def drop_duplicates(self, subset=None, keep="first", inplace=False):
        return DataFrame()

    @property
    def values(self):
        return []

    @property
    def index(self):
        return Index()

    @property
    def dtypes(self):
        return Series()

    @property
    def iloc(self):
        return _Accessor()

    @property
    def loc(self):
        return _Accessor()

    @property
    def at(self):
        return _Accessor()

    @property
    def iat(self):
        return _Accessor()

    @property
    def T(self):
        return DataFrame()

    @property
    def empty(self) -> bool:
        return True


class Index:
    def __init__(self, data=None, dtype=None, name=None):
        self.name = name
        self.dtype = dtype

    def __len__(self) -> int:
        return 0

    def __getitem__(self, key):
        return 0

    def tolist(self):
        return []


class _Accessor:
    """Model for .loc / .iloc / .at / .iat."""

    def __getitem__(self, key):
        return Series() if isinstance(key, slice) else 0

    def __setitem__(self, key, value) -> None:
        return None


class _StrAccessor:
    """Series.str string-operation accessor."""

    def lower(self):
        return Series()

    def upper(self):
        return Series()

    def strip(self):
        return Series()

    def contains(self, pat, case=True, flags=0, na=None, regex=True):
        return Series()

    def startswith(self, pat, na=None):
        return Series()

    def endswith(self, pat, na=None):
        return Series()

    def replace(self, pat, repl, n=-1, case=None, flags=0, regex=True):
        return Series()

    def split(self, pat=None, n=-1, expand=False):
        return Series()

    def len(self):
        return Series()


class _GroupBy:
    def sum(self):
        return DataFrame()

    def mean(self):
        return DataFrame()

    def count(self):
        return DataFrame()

    def size(self):
        return Series()

    def min(self):
        return DataFrame()

    def max(self):
        return DataFrame()

    def agg(self, func):
        return DataFrame()

    def apply(self, func):
        return DataFrame()


def read_csv(filepath_or_buffer, sep=",", header=0, names=None, index_col=None,
             usecols=None, dtype=None, engine=None, skiprows=None, nrows=None,
             encoding=None, **kwargs):
    return DataFrame()


def read_excel(io, sheet_name=0, header=0, names=None, index_col=None,
               usecols=None, dtype=None, skiprows=None, nrows=None, **kwargs):
    return DataFrame()


def read_json(path_or_buf, orient=None, typ="frame", dtype=None, **kwargs):
    return DataFrame()


def read_parquet(path, engine="auto", columns=None, **kwargs):
    return DataFrame()


def read_sql(sql, con, index_col=None, **kwargs):
    return DataFrame()


def concat(objs, axis=0, join="outer", ignore_index=False, keys=None):
    return DataFrame()


def merge(left, right, how="inner", on=None, left_on=None, right_on=None):
    return DataFrame()


def date_range(start=None, end=None, periods=None, freq=None, tz=None,
               name=None):
    return Index()


def to_datetime(arg, errors="raise", format=None, utc=False):
    return Series()


def isna(obj):
    return False


def notna(obj):
    return True


def isnull(obj):
    return False


def notnull(obj):
    return True


# Common options shim
class _Options:
    pass


options = _Options()
