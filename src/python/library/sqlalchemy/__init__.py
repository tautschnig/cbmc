"""
Verification model of the third-party `sqlalchemy` package.

Database I/O is not modelled. Engines, sessions, queries,
and ORM classes are opaque carriers that accept the
common API. Method calls return nondet / empty results.
"""


class SQLAlchemyError(Exception):
    pass


class DatabaseError(SQLAlchemyError):
    pass


class OperationalError(DatabaseError):
    pass


class IntegrityError(DatabaseError):
    pass


class NoResultFound(SQLAlchemyError):
    pass


class MultipleResultsFound(SQLAlchemyError):
    pass


# SQL-type placeholders. Library code uses these as class
# tokens; we expose them as identity objects.
class _Type:
    def __init__(self, *args, **kwargs):
        pass


Integer = _Type
String = _Type
Float = _Type
Boolean = _Type
DateTime = _Type
Date = _Type
Text = _Type
LargeBinary = _Type
Numeric = _Type
Enum = _Type
JSON = _Type
UUID = _Type


class Column:
    def __init__(self, *args, **kwargs):
        self.name = kwargs.get("name", "")
        self.type = args[0] if args else None
        self.primary_key = kwargs.get("primary_key", False)
        self.nullable = kwargs.get("nullable", True)


class ForeignKey:
    def __init__(self, target, *args, **kwargs):
        self.target = target


class Table:
    def __init__(self, name, metadata=None, *args, **kwargs):
        self.name = name
        self.columns = args

    def select(self, *args, **kwargs):
        return _Query()


class MetaData:
    def __init__(self, bind=None):
        self.bind = bind
        self.tables = {}

    def create_all(self, bind=None, tables=None, checkfirst=True):
        return None

    def drop_all(self, bind=None, tables=None, checkfirst=True):
        return None

    def reflect(self, bind=None, schema=None, views=False, only=None,
                extend_existing=False):
        return None


class Engine:
    def __init__(self, url=""):
        self.url = url

    def connect(self):
        return Connection()

    def dispose(self) -> None:
        return None

    def execute(self, stmt, *args, **kwargs):
        return _Result()

    def begin(self):
        return Connection()


class Connection:
    def __init__(self):
        pass

    def execute(self, stmt, *args, **kwargs):
        return _Result()

    def close(self) -> None:
        return None

    def commit(self) -> None:
        return None

    def rollback(self) -> None:
        return None

    def begin(self):
        return self

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
        return False


class _Result:
    def __init__(self):
        self.rowcount = 0

    def fetchone(self):
        return None

    def fetchall(self):
        return []

    def fetchmany(self, size=None):
        return []

    def scalars(self):
        return self

    def scalar(self):
        return None

    def scalar_one(self):
        return None

    def scalar_one_or_none(self):
        return None

    def all(self):
        return []

    def first(self):
        return None

    def one(self):
        return None

    def one_or_none(self):
        return None

    def __iter__(self):
        return iter([])


class _Query:
    def filter(self, *criterion):
        return self

    def filter_by(self, **kwargs):
        return self

    def all(self):
        return []

    def first(self):
        return None

    def one(self):
        return None

    def count(self) -> int:
        return 0

    def limit(self, n: int):
        return self

    def offset(self, n: int):
        return self

    def order_by(self, *columns):
        return self

    def join(self, *args, **kwargs):
        return self

    def outerjoin(self, *args, **kwargs):
        return self

    def group_by(self, *columns):
        return self

    def having(self, *criterion):
        return self

    def distinct(self, *columns):
        return self


class Session:
    def __init__(self, bind=None, **kwargs):
        self.bind = bind

    def add(self, obj) -> None:
        return None

    def add_all(self, instances) -> None:
        return None

    def commit(self) -> None:
        return None

    def rollback(self) -> None:
        return None

    def close(self) -> None:
        return None

    def flush(self) -> None:
        return None

    def refresh(self, obj) -> None:
        return None

    def delete(self, obj) -> None:
        return None

    def merge(self, obj):
        return obj

    def query(self, *entities):
        return _Query()

    def execute(self, stmt, *args, **kwargs):
        return _Result()

    def get(self, entity, ident):
        return None

    def scalar(self, stmt, *args, **kwargs):
        return None

    def scalars(self, stmt, *args, **kwargs):
        return _Result()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
        return False


def sessionmaker(bind=None, **kwargs):
    def factory(*args, **kw):
        return Session(bind=bind)
    return factory


def create_engine(url, **kwargs) -> Engine:
    return Engine(url)


def select(*entities):
    return _Query()


def update(*tables):
    return _Query()


def delete(*tables):
    return _Query()


def insert(table):
    return _Query()


def func_placeholder():
    return 0


class func:
    """SQL function namespace — func.count(), func.max(), etc."""

    @staticmethod
    def count(*args):
        return _Type()

    @staticmethod
    def sum(*args):
        return _Type()

    @staticmethod
    def max(*args):
        return _Type()

    @staticmethod
    def min(*args):
        return _Type()

    @staticmethod
    def avg(*args):
        return _Type()

    @staticmethod
    def now():
        return _Type()


def and_(*clauses):
    return _Type()


def or_(*clauses):
    return _Type()


def not_(clause):
    return _Type()


def text(sql: str):
    return _Type()


def bindparam(key: str, *args, **kwargs):
    return _Type()


# declarative base factory returns a class; for
# verification we return a simple opaque base.
def declarative_base(**kwargs):
    class Base:
        __tablename__ = ""
    return Base


class DeclarativeBase:
    __tablename__ = ""


class Mapped:
    pass


def mapped_column(*args, **kwargs):
    return Column(*args, **kwargs)


def relationship(*args, **kwargs):
    return _Type()
