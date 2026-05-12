"""
Verification model of the third-party `rich` terminal
formatting library.

The print/log APIs are modelled as no-ops; objects like
Console and Table are opaque with no-op methods.
"""


class Console:
    def __init__(self, **kwargs):
        self.file = None

    def print(self, *args, **kwargs) -> None:
        return None

    def log(self, *args, **kwargs) -> None:
        return None

    def rule(self, title: str = "", **kwargs) -> None:
        return None

    def status(self, message: str = "", **kwargs):
        return self


class Table:
    def __init__(self, title: str = "", **kwargs):
        self.title = title

    def add_column(self, header: str = "", **kwargs) -> None:
        return None

    def add_row(self, *cells) -> None:
        return None


class Panel:
    def __init__(self, renderable, **kwargs):
        self.renderable = renderable


class Progress:
    def __init__(self, *columns, **kwargs):
        self.columns = columns

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, tb) -> bool:
        return False

    def add_task(self, description: str = "", total: int = 0) -> int:
        return 0

    def update(self, task_id: int, **kwargs) -> None:
        return None

    def advance(self, task_id: int, amount: int = 1) -> None:
        return None


def print(*args, **kwargs) -> None:
    return None


def inspect(obj, **kwargs) -> None:
    return None
