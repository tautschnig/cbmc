"""
Verification model of the `logging` module.

Every logger produced here is a silent no-op: ``.info`` / ``.debug``
/ ``.warning`` / ``.error`` / ``.critical`` / ``.exception`` /
``.log`` all accept arbitrary args and return None. Handlers and
formatters are stubbed the same way so code that sets up logging
infrastructure runs without error.

Verification reasons over user-level behaviour, not logging
output; stripping the log side effect is a sound
over-approximation.
"""


DEBUG = 10
INFO = 20
WARNING = 30
WARN = WARNING
ERROR = 40
CRITICAL = 50
FATAL = CRITICAL
NOTSET = 0


class Logger:
    def __init__(self, name: str = "root", level: int = NOTSET):
        self.name = name
        self.level = level
        self.handlers = []
        self.propagate = True
        self.parent = None

    def setLevel(self, level: int):
        self.level = level

    def isEnabledFor(self, level: int):
        return True

    def debug(self, msg, *args, **kwargs):
        return None

    def info(self, msg, *args, **kwargs):
        return None

    def warning(self, msg, *args, **kwargs):
        return None

    warn = warning

    def error(self, msg, *args, **kwargs):
        return None

    def critical(self, msg, *args, **kwargs):
        return None

    fatal = critical

    def exception(self, msg, *args, **kwargs):
        return None

    def log(self, level, msg, *args, **kwargs):
        return None

    def addHandler(self, hdlr):
        self.handlers.append(hdlr)

    def removeHandler(self, hdlr):
        if hdlr in self.handlers:
            self.handlers.remove(hdlr)

    def getChild(self, suffix: str):
        return Logger(self.name + "." + suffix, self.level)

    def hasHandlers(self):
        return len(self.handlers) > 0


class Handler:
    def __init__(self, level: int = NOTSET):
        self.level = level

    def setLevel(self, level):
        self.level = level

    def setFormatter(self, fmt):
        return None

    def emit(self, record):
        return None

    def handle(self, record):
        return None

    def flush(self):
        return None

    def close(self):
        return None


class StreamHandler(Handler):
    def __init__(self, stream=None):
        super().__init__()
        self.stream = stream


class FileHandler(Handler):
    def __init__(self, filename: str, mode: str = "a", encoding=None,
                 delay: bool = False, errors=None):
        super().__init__()
        self.filename = filename


class NullHandler(Handler):
    pass


class Formatter:
    def __init__(self, fmt=None, datefmt=None, style: str = "%",
                 validate: bool = True, *, defaults=None):
        self.fmt = fmt

    def format(self, record):
        return nondet_str()


class LogRecord:
    def __init__(self, name, level, pathname, lineno, msg, args,
                 exc_info, func=None, sinfo=None):
        self.name = name
        self.msg = msg


_root_logger = Logger("root")


def getLogger(name: str = None):
    return Logger(name or "root")


def basicConfig(**kwargs):
    return None


def debug(msg, *args, **kwargs):
    return None


def info(msg, *args, **kwargs):
    return None


def warning(msg, *args, **kwargs):
    return None


warn = warning


def error(msg, *args, **kwargs):
    return None


def critical(msg, *args, **kwargs):
    return None


def exception(msg, *args, **kwargs):
    return None


def log(level, msg, *args, **kwargs):
    return None


def disable(level: int = CRITICAL):
    return None


def shutdown():
    return None


def getLevelName(level):
    return "INFO"


def addLevelName(level, name):
    return None


def captureWarnings(capture: bool):
    return None
