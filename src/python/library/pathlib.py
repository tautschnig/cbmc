"""
Verification model of the `pathlib` module (partial).

Covers ``PurePath``, ``PurePosixPath``, ``PureWindowsPath``,
``Path``, ``PosixPath``, ``WindowsPath`` — all with shared attribute
shape. Paths expose the common query methods (``exists``, ``is_file``,
``is_dir``, ``stem``, ``suffix``, ``parent``, ``name``, ...) as nondet
stand-ins or simple string returns. Filesystem-mutating methods
(``mkdir``, ``unlink``, ``rename``, ``write_text``, ...) are
modelled as no-ops.
"""


class PurePath:
    """Immutable path base. Attributes are read-only in CPython;
    for verification we expose them as annotated string / tuple
    fields."""

    _parts: tuple
    _raw: str

    def __init__(self, *parts):
        self._raw = ""
        self._parts = parts

    # Attribute-style accessors
    @property
    def name(self) -> str:
        return ""

    @property
    def suffix(self) -> str:
        return ""

    @property
    def suffixes(self):
        return []

    @property
    def stem(self) -> str:
        return ""

    @property
    def parent(self):
        return PurePath()

    @property
    def parents(self):
        return ()

    @property
    def parts(self):
        return self._parts

    @property
    def root(self) -> str:
        return ""

    @property
    def drive(self) -> str:
        return ""

    @property
    def anchor(self) -> str:
        return ""

    # Construction
    def with_name(self, name: str):
        return PurePath()

    def with_suffix(self, suffix: str):
        return PurePath()

    def with_stem(self, stem: str):
        return PurePath()

    def joinpath(self, *other):
        return PurePath()

    def __truediv__(self, other):
        return PurePath()

    def __rtruediv__(self, other):
        return PurePath()

    # Query
    def is_absolute(self) -> bool:
        return False

    def is_relative_to(self, *other) -> bool:
        return False

    def relative_to(self, *other):
        return PurePath()

    def match(self, pattern: str) -> bool:
        return False

    def as_posix(self) -> str:
        return ""

    def as_uri(self) -> str:
        return ""

    def __fspath__(self) -> str:
        return ""

    def __str__(self) -> str:
        return ""


class PurePosixPath(PurePath):
    pass


class PureWindowsPath(PurePath):
    pass


class Path(PurePath):
    """Concrete path (adds I/O methods)."""

    # Instance factory
    @classmethod
    def cwd(cls):
        return Path()

    @classmethod
    def home(cls):
        return Path()

    # File-system queries
    def exists(self) -> bool:
        return False

    def is_file(self) -> bool:
        return False

    def is_dir(self) -> bool:
        return False

    def is_symlink(self) -> bool:
        return False

    def is_mount(self) -> bool:
        return False

    def is_block_device(self) -> bool:
        return False

    def is_char_device(self) -> bool:
        return False

    def is_fifo(self) -> bool:
        return False

    def is_socket(self) -> bool:
        return False

    def stat(self):
        return None

    def lstat(self):
        return None

    def resolve(self, strict: bool = False):
        return Path()

    def absolute(self):
        return Path()

    def expanduser(self):
        return Path()

    def glob(self, pattern: str):
        return []

    def rglob(self, pattern: str):
        return []

    def iterdir(self):
        return []

    def owner(self) -> str:
        return ""

    def group(self) -> str:
        return ""

    def readlink(self):
        return Path()

    # File-system mutations (modelled as no-ops)
    def mkdir(
        self,
        mode: int = 0,
        parents: bool = False,
        exist_ok: bool = False,
    ) -> None:
        return None

    def rmdir(self) -> None:
        return None

    def unlink(self, missing_ok: bool = False) -> None:
        return None

    def rename(self, target):
        return Path()

    def replace(self, target):
        return Path()

    def symlink_to(self, target, target_is_directory: bool = False) -> None:
        return None

    def hardlink_to(self, target) -> None:
        return None

    def touch(self, mode: int = 0, exist_ok: bool = True) -> None:
        return None

    def chmod(self, mode: int, *, follow_symlinks: bool = True) -> None:
        return None

    def lchmod(self, mode: int) -> None:
        return None

    # File I/O (nondet content)
    def open(
        self,
        mode: str = "r",
        buffering: int = -1,
        encoding=None,
        errors=None,
        newline=None,
    ):
        return None

    def read_text(self, encoding=None, errors=None) -> str:
        return ""

    def read_bytes(self):
        return b""

    def write_text(
        self,
        data: str,
        encoding=None,
        errors=None,
        newline=None,
    ) -> int:
        return 0

    def write_bytes(self, data) -> int:
        return 0


class PosixPath(Path):
    pass


class WindowsPath(Path):
    pass
