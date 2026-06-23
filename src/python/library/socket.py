"""
Verification model of the `socket` module.

Socket I/O is not modelled. The API surface is available
so code that imports and type-annotates against socket
objects parses cleanly; actual network I/O is replaced by
nondet reads and no-op writes.
"""


# Address families
AF_INET = 2
AF_INET6 = 10
AF_UNIX = 1
AF_UNSPEC = 0

# Socket types
SOCK_STREAM = 1
SOCK_DGRAM = 2
SOCK_RAW = 3

# Protocols
IPPROTO_TCP = 6
IPPROTO_UDP = 17

# Options
SOL_SOCKET = 1
SO_REUSEADDR = 2
SO_KEEPALIVE = 9
SO_BROADCAST = 6
SO_LINGER = 13
SO_ERROR = 4
SO_RCVBUF = 8
SO_SNDBUF = 7

# Shutdown flags
SHUT_RD = 0
SHUT_WR = 1
SHUT_RDWR = 2


class error(OSError):
    pass


class herror(error):
    pass


class gaierror(error):
    pass


class timeout(error):
    pass


class socket:
    def __init__(self, family: int = AF_INET, type: int = SOCK_STREAM,
                 proto: int = 0, fileno=None):
        self.family = family
        self.type = type
        self.proto = proto
        self._closed = False

    def connect(self, address) -> None:
        return None

    def bind(self, address) -> None:
        return None

    def listen(self, backlog: int = 0) -> None:
        return None

    def accept(self):
        return (socket(), ("", 0))

    def send(self, data, flags: int = 0) -> int:
        return nondet_int()

    def sendall(self, data, flags: int = 0) -> None:
        return None

    def sendto(self, data, *args) -> int:
        return nondet_int()

    def recv(self, bufsize: int, flags: int = 0) -> bytes:
        return b""

    def recvfrom(self, bufsize: int, flags: int = 0):
        return (b"", ("", 0))

    def close(self) -> None:
        self._closed = True

    def shutdown(self, how: int) -> None:
        return None

    def getsockname(self):
        return ("", 0)

    def getpeername(self):
        return ("", 0)

    def setsockopt(self, level: int, optname: int, value) -> None:
        return None

    def getsockopt(self, level: int, optname: int, buflen: int = 0) -> int:
        return nondet_int()

    def settimeout(self, timeout) -> None:
        return None

    def gettimeout(self):
        return None

    def setblocking(self, flag: bool) -> None:
        return None

    def fileno(self) -> int:
        return nondet_int()

    def makefile(self, mode: str = "r", buffering=None, *, encoding=None,
                 errors=None, newline=None):
        return None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
        return False


def gethostname() -> str:
    return nondet_str()


def gethostbyname(hostname: str) -> str:
    return "0.0.0.0"


def gethostbyaddr(ip: str):
    return ("", [], [ip])


def getaddrinfo(host, port, family: int = 0, type: int = 0, proto: int = 0,
                flags: int = 0):
    return [(AF_INET, SOCK_STREAM, IPPROTO_TCP, "", (host, port))]


def getservbyname(servicename: str, protocolname: str = "") -> int:
    return nondet_int()


def getnameinfo(sockaddr, flags: int):
    return ("", "")


def inet_aton(ip: str) -> bytes:
    return b"\x00\x00\x00\x00"


def inet_ntoa(packed: bytes) -> str:
    return "0.0.0.0"


def inet_pton(family: int, ip: str) -> bytes:
    return b"\x00\x00\x00\x00"


def inet_ntop(family: int, packed: bytes) -> str:
    return "0.0.0.0"


def create_connection(address, timeout=None, source_address=None):
    return socket()


def create_server(address, family: int = AF_INET, backlog=None,
                  reuse_port: bool = False):
    return socket()
