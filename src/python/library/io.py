"""
Verification model of the `io` module.

Covers the file-like class hierarchy: IOBase, RawIOBase, BufferedIOBase,
TextIOBase, plus StringIO and BytesIO. Every operation is a nondet
over-approximation — reading produces nondet bytes/text, writing is
a no-op. Enough to let user code parse and run without missing-body
errors.
"""


DEFAULT_BUFFER_SIZE = 8192
SEEK_SET = 0
SEEK_CUR = 1
SEEK_END = 2


class IOBase:
    def __init__(self):
        self.closed = False

    def close(self):
        self.closed = True

    def fileno(self):
        return -1

    def flush(self):
        return None

    def isatty(self):
        return False

    def readable(self):
        return True

    def writable(self):
        return True

    def seekable(self):
        return True

    def seek(self, offset, whence=SEEK_SET):
        return 0

    def tell(self):
        return nondet_int()

    def truncate(self, size=None):
        return nondet_int()

    def __iter__(self):
        return self

    def __next__(self):
        raise StopIteration

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
        return False


class RawIOBase(IOBase):
    def read(self, size=-1):
        return b""

    def readall(self):
        return b""

    def readinto(self, b):
        return 0

    def write(self, b):
        return len(b) if hasattr(b, "__len__") else 0


class BufferedIOBase(IOBase):
    raw = None

    def read(self, size=-1):
        return b""

    def read1(self, size=-1):
        return b""

    def readinto(self, b):
        return 0

    def readinto1(self, b):
        return 0

    def write(self, b):
        return len(b) if hasattr(b, "__len__") else 0

    def detach(self):
        return self.raw


class TextIOBase(IOBase):
    encoding = "utf-8"
    errors = "strict"
    newlines = None

    def read(self, size=-1):
        return ""

    def readline(self, size=-1):
        return nondet_str()

    def readlines(self, hint=-1):
        return nondet_list(8, nondet_str())

    def write(self, s):
        return len(s)

    def writelines(self, lines):
        return None


class StringIO(TextIOBase):
    def __init__(self, initial_value: str = "", newline: str = "\n"):
        super().__init__()
        self._value = initial_value

    def getvalue(self):
        return self._value

    def read(self, size=-1):
        return self._value

    def write(self, s):
        return len(s)


class BytesIO(BufferedIOBase):
    def __init__(self, initial_bytes: bytes = b""):
        super().__init__()
        self._value = initial_bytes

    def getvalue(self):
        return self._value

    def getbuffer(self):
        return memoryview(self._value)

    def read(self, size=-1):
        return self._value

    def write(self, b):
        return len(b) if hasattr(b, "__len__") else 0


class FileIO(RawIOBase):
    def __init__(self, name, mode="r", closefd=True, opener=None):
        super().__init__()
        self.name = name
        self.mode = mode


class BufferedReader(BufferedIOBase):
    def __init__(self, raw, buffer_size=DEFAULT_BUFFER_SIZE):
        super().__init__()
        self.raw = raw


class BufferedWriter(BufferedIOBase):
    def __init__(self, raw, buffer_size=DEFAULT_BUFFER_SIZE):
        super().__init__()
        self.raw = raw


class BufferedRandom(BufferedIOBase):
    def __init__(self, raw, buffer_size=DEFAULT_BUFFER_SIZE):
        super().__init__()
        self.raw = raw


class TextIOWrapper(TextIOBase):
    def __init__(self, buffer, encoding=None, errors=None, newline=None,
                 line_buffering=False, write_through=False):
        super().__init__()
        self.buffer = buffer
        if encoding is not None:
            self.encoding = encoding


class UnsupportedOperation(OSError, ValueError):
    pass


# Module-level helpers
def open(file, mode="r", buffering=-1, encoding=None, errors=None,
         newline=None, closefd=True, opener=None):
    if "b" in mode:
        return BytesIO()
    return StringIO()
