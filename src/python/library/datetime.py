"""
Verification-optimized model of the `datetime` module.

The real CPython `datetime.py` is a redirect to the C-implemented
`_datetime` with a Python fallback in `_pydatetime.py`. Neither is
straightforward for us to ingest. This stub exposes the public
classes and their most common methods as nondet-returning shapes.
"""


MINYEAR = 1
MAXYEAR = 9999


class timedelta:
    """Difference between two datetime instances, modelled as an
    opaque container with the three canonical fields."""

    days: int
    seconds: int
    microseconds: int

    min = None  # set at module bottom
    max = None
    resolution = None

    def __init__(self, days: int = 0, seconds: int = 0, microseconds: int = 0,
                 milliseconds: int = 0, minutes: int = 0, hours: int = 0,
                 weeks: int = 0):
        self.days = days
        self.seconds = seconds
        self.microseconds = microseconds

    def total_seconds(self) -> float:
        return 0.0


class tzinfo:
    """Abstract timezone base class."""

    def utcoffset(self, dt):
        return None

    def dst(self, dt):
        return None

    def tzname(self, dt) -> str:
        return ""

    def fromutc(self, dt):
        return dt


class timezone(tzinfo):
    utc = None  # set below

    def __init__(self, offset=None, name: str = ""):
        return None


class date:
    """A naive calendar date."""

    year: int
    month: int
    day: int

    min = None
    max = None
    resolution = None

    def __init__(self, year: int = 1, month: int = 1, day: int = 1):
        self.year = year
        self.month = month
        self.day = day

    @classmethod
    def today(cls):
        return date()

    @classmethod
    def fromtimestamp(cls, t):
        return date()

    @classmethod
    def fromordinal(cls, n: int):
        return date()

    @classmethod
    def fromisoformat(cls, s: str):
        return date()

    def replace(self, year=None, month=None, day=None):
        return self

    def timetuple(self):
        return None

    def toordinal(self) -> int:
        return 0

    def weekday(self) -> int:
        return 0

    def isoweekday(self) -> int:
        return 0

    def isocalendar(self):
        return (0, 0, 0)

    def isoformat(self) -> str:
        return ""

    def strftime(self, fmt: str) -> str:
        return ""


class time:
    """A time, independent of any particular day."""

    hour: int
    minute: int
    second: int
    microsecond: int

    min = None
    max = None
    resolution = None

    def __init__(self, hour: int = 0, minute: int = 0, second: int = 0,
                 microsecond: int = 0, tzinfo=None, *, fold: int = 0):
        self.hour = hour
        self.minute = minute
        self.second = second
        self.microsecond = microsecond

    @classmethod
    def fromisoformat(cls, s: str):
        return time()

    def replace(self, hour=None, minute=None, second=None,
                microsecond=None, tzinfo=None, *, fold=None):
        return self

    def utcoffset(self):
        return None

    def dst(self):
        return None

    def tzname(self) -> str:
        return ""

    def isoformat(self, timespec: str = "auto") -> str:
        return ""

    def strftime(self, fmt: str) -> str:
        return ""


class datetime(date):
    """A date plus a time."""

    hour: int
    minute: int
    second: int
    microsecond: int

    def __init__(self, year: int = 1, month: int = 1, day: int = 1,
                 hour: int = 0, minute: int = 0, second: int = 0,
                 microsecond: int = 0, tzinfo=None, *, fold: int = 0):
        self.year = year
        self.month = month
        self.day = day
        self.hour = hour
        self.minute = minute
        self.second = second
        self.microsecond = microsecond

    @classmethod
    def now(cls, tz=None):
        return datetime()

    @classmethod
    def utcnow(cls):
        return datetime()

    @classmethod
    def fromtimestamp(cls, t, tz=None):
        return datetime()

    @classmethod
    def utcfromtimestamp(cls, t):
        return datetime()

    @classmethod
    def fromordinal(cls, n: int):
        return datetime()

    @classmethod
    def fromisoformat(cls, s: str):
        return datetime()

    @classmethod
    def combine(cls, d, t, tzinfo=None):
        return datetime()

    @classmethod
    def strptime(cls, s: str, fmt: str):
        return datetime()

    def timestamp(self) -> float:
        return 0.0

    def date(self):
        return date()

    def time(self):
        return time()

    def timetz(self):
        return time()

    def replace(self, year=None, month=None, day=None, hour=None, minute=None,
                second=None, microsecond=None, tzinfo=None, *, fold=None):
        return self

    def astimezone(self, tz=None):
        return self

    def utcoffset(self):
        return None

    def dst(self):
        return None

    def tzname(self) -> str:
        return ""

    def isoformat(self, sep: str = "T", timespec: str = "auto") -> str:
        return ""

    def strftime(self, fmt: str) -> str:
        return ""


# Singleton objects exposed by the module.
UTC = None
