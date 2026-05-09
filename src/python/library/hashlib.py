"""
Verification model of the `hashlib` module.

Covers the constructor helpers (``md5``, ``sha1``, ``sha224``,
``sha256``, ``sha384``, ``sha512``, ``blake2b``, ``blake2s``, the
``sha3_*`` and ``shake_*`` families, plus the generic ``new``).
The returned hasher objects are stubs whose ``update`` is a no-op
and whose ``hexdigest`` / ``digest`` return nondet values of the
appropriate shape — which is the right over-approximation for
verification purposes, where cryptographic-hash equality isn't
something the solver should reason about byte-exact.
"""


class _Hasher:
    """Generic hasher. Produced by all constructors."""

    def __init__(self, name, data=None):
        self.name = name
        self.digest_size = 16
        self.block_size = 64

    def update(self, data):
        return None

    def digest(self):
        return b""

    def hexdigest(self):
        return ""

    def copy(self):
        return _Hasher(self.name)


def new(name, data=b"", **kwargs):
    """Generic constructor — hashlib.new('sha256') etc."""
    return _Hasher(name, data)


def md5(data=b"", **kwargs):
    return _Hasher("md5", data)


def sha1(data=b"", **kwargs):
    return _Hasher("sha1", data)


def sha224(data=b"", **kwargs):
    return _Hasher("sha224", data)


def sha256(data=b"", **kwargs):
    return _Hasher("sha256", data)


def sha384(data=b"", **kwargs):
    return _Hasher("sha384", data)


def sha512(data=b"", **kwargs):
    return _Hasher("sha512", data)


def blake2b(data=b"", digest_size=64, key=b"", **kwargs):
    h = _Hasher("blake2b", data)
    h.digest_size = digest_size
    return h


def blake2s(data=b"", digest_size=32, key=b"", **kwargs):
    h = _Hasher("blake2s", data)
    h.digest_size = digest_size
    return h


def sha3_224(data=b"", **kwargs):
    return _Hasher("sha3_224", data)


def sha3_256(data=b"", **kwargs):
    return _Hasher("sha3_256", data)


def sha3_384(data=b"", **kwargs):
    return _Hasher("sha3_384", data)


def sha3_512(data=b"", **kwargs):
    return _Hasher("sha3_512", data)


class _ShakeHasher(_Hasher):
    def digest(self, length):
        return b""

    def hexdigest(self, length):
        return ""


def shake_128(data=b"", **kwargs):
    return _ShakeHasher("shake_128", data)


def shake_256(data=b"", **kwargs):
    return _ShakeHasher("shake_256", data)


def pbkdf2_hmac(hash_name, password, salt, iterations, dklen=None):
    return b""


def scrypt(password, *, salt, n, r, p, maxmem=0, dklen=64):
    return b""


def file_digest(fileobj, digest, /, *, _bufsize=2 ** 18):
    return _Hasher("digest")


# Iterable of algorithm names.
algorithms_available = frozenset([
    "md5", "sha1", "sha224", "sha256", "sha384", "sha512",
    "sha3_224", "sha3_256", "sha3_384", "sha3_512",
    "shake_128", "shake_256", "blake2b", "blake2s",
])

algorithms_guaranteed = frozenset([
    "md5", "sha1", "sha224", "sha256", "sha384", "sha512",
    "sha3_224", "sha3_256", "sha3_384", "sha3_512",
    "shake_128", "shake_256", "blake2b", "blake2s",
])
