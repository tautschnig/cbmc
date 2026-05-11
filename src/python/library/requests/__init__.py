"""
Verification model of the third-party `requests` module.

HTTP client I/O is not modelled. Calls return Response
objects with nondet status codes and empty bodies.
"""


class RequestException(Exception):
    pass


class HTTPError(RequestException):
    pass


class ConnectionError(RequestException):
    pass


class Timeout(RequestException):
    pass


class ConnectTimeout(Timeout, ConnectionError):
    pass


class ReadTimeout(Timeout):
    pass


class URLRequired(RequestException):
    pass


class TooManyRedirects(RequestException):
    pass


class MissingSchema(RequestException):
    pass


class InvalidSchema(RequestException):
    pass


class InvalidURL(RequestException):
    pass


class ChunkedEncodingError(RequestException):
    pass


class ContentDecodingError(RequestException):
    pass


class StreamConsumedError(RequestException, TypeError):
    pass


class RetryError(RequestException):
    pass


class UnrewindableBodyError(RequestException):
    pass


class Response:
    def __init__(self):
        self.status_code = 200
        self.headers = {}
        self.content = b""
        self.text = ""
        self.url = ""
        self.encoding = None
        self.cookies = {}
        self.history = []
        self.reason = "OK"
        self.elapsed = 0.0
        self.ok = True

    def json(self, **kwargs):
        return {}

    def raise_for_status(self) -> None:
        if self.status_code >= 400:
            raise HTTPError()

    def iter_content(self, chunk_size: int = 1, decode_unicode: bool = False):
        return iter([])

    def iter_lines(self, chunk_size: int = 512, decode_unicode: bool = False,
                   delimiter=None):
        return iter([])

    def close(self) -> None:
        return None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
        return False


class Request:
    def __init__(self, method=None, url=None, headers=None, files=None,
                 data=None, params=None, auth=None, cookies=None, hooks=None,
                 json=None):
        self.method = method
        self.url = url
        self.headers = headers if headers is not None else {}
        self.files = files
        self.data = data
        self.params = params
        self.auth = auth
        self.cookies = cookies
        self.hooks = hooks
        self.json = json

    def prepare(self):
        return PreparedRequest()


class PreparedRequest:
    def __init__(self):
        self.method = None
        self.url = None
        self.headers = {}
        self.body = None


class Session:
    def __init__(self):
        self.headers = {}
        self.cookies = {}
        self.auth = None
        self.verify = True

    def get(self, url, **kwargs) -> Response:
        return Response()

    def post(self, url, data=None, json=None, **kwargs) -> Response:
        return Response()

    def put(self, url, data=None, **kwargs) -> Response:
        return Response()

    def delete(self, url, **kwargs) -> Response:
        return Response()

    def patch(self, url, data=None, **kwargs) -> Response:
        return Response()

    def head(self, url, **kwargs) -> Response:
        return Response()

    def options(self, url, **kwargs) -> Response:
        return Response()

    def request(self, method, url, **kwargs) -> Response:
        return Response()

    def send(self, request, **kwargs) -> Response:
        return Response()

    def close(self) -> None:
        return None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
        return False


def get(url, params=None, **kwargs) -> Response:
    return Response()


def post(url, data=None, json=None, **kwargs) -> Response:
    return Response()


def put(url, data=None, **kwargs) -> Response:
    return Response()


def delete(url, **kwargs) -> Response:
    return Response()


def patch(url, data=None, **kwargs) -> Response:
    return Response()


def head(url, **kwargs) -> Response:
    return Response()


def options(url, **kwargs) -> Response:
    return Response()


def request(method, url, **kwargs) -> Response:
    return Response()


def session() -> Session:
    return Session()
