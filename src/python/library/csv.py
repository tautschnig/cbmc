"""
Verification model of the `csv` module.

Covers ``reader``, ``writer``, ``DictReader``, ``DictWriter``, the
``Dialect`` classes and the ``Sniffer``. The parser isn't actually
run — reads produce nondet rows, writes are no-ops. This is the
correct over-approximation for verification of code that merely
manipulates CSV data.
"""


# Quoting constants
QUOTE_MINIMAL = 0
QUOTE_ALL = 1
QUOTE_NONNUMERIC = 2
QUOTE_NONE = 3
QUOTE_STRINGS = 4
QUOTE_NOTNULL = 5


class Dialect:
    """Base Dialect. Subclasses set dialect-specific attributes."""

    delimiter = ","
    quotechar = '"'
    escapechar = None
    doublequote = True
    skipinitialspace = False
    lineterminator = "\r\n"
    quoting = QUOTE_MINIMAL
    strict = False


class excel(Dialect):
    pass


class excel_tab(excel):
    delimiter = "\t"


class unix_dialect(Dialect):
    quotechar = '"'
    quoting = QUOTE_ALL
    lineterminator = "\n"


class Error(Exception):
    pass


# Global dialect registry. register_dialect / unregister_dialect /
# get_dialect / list_dialects are no-ops at verification time.
_dialects = {"excel": excel, "excel-tab": excel_tab, "unix": unix_dialect}


def register_dialect(name, dialect=None, **fmtparams):
    _dialects[name] = dialect or Dialect


def unregister_dialect(name):
    _dialects.pop(name, None)


def get_dialect(name):
    return _dialects.get(name, excel)


def list_dialects():
    return list(_dialects.keys())


field_size_limit = 131072


class _CsvReader:
    def __init__(self):
        self.line_num = 0

    def __iter__(self):
        return self

    def __next__(self):
        raise StopIteration


class _CsvWriter:
    def __init__(self):
        self.line = 0

    def writerow(self, row):
        return 0

    def writerows(self, rows):
        return 0


def reader(csvfile, dialect="excel", **fmtparams):
    """Return a reader that iterates nondet rows."""
    return _CsvReader()


def writer(csvfile, dialect="excel", **fmtparams):
    """Return a writer; writerow / writerows are no-ops."""
    return _CsvWriter()


class DictReader:
    """Nondet DictReader; treats every row as an empty dict and
    stops immediately."""

    def __init__(self, f, fieldnames=None, restkey=None, restval=None,
                 dialect="excel", *args, **kwargs):
        self.fieldnames = fieldnames or []
        self.restkey = restkey
        self.restval = restval
        self.line_num = 0

    def __iter__(self):
        return self

    def __next__(self):
        raise StopIteration


class DictWriter:
    def __init__(self, f, fieldnames, restval="", extrasaction="raise",
                 dialect="excel", *args, **kwargs):
        self.fieldnames = fieldnames

    def writeheader(self):
        return 0

    def writerow(self, rowdict):
        return 0

    def writerows(self, rowdicts):
        return 0


class Sniffer:
    def sniff(self, sample, delimiters=None):
        return excel

    def has_header(self, sample):
        return False
