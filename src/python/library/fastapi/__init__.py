"""
Verification model of the third-party `fastapi` web
framework (Starlette-based, async).

Decorators are no-ops for verification — the decorated
coroutine/function is returned unchanged. Request models
are empty structs.
"""


class HTTPException(Exception):
    def __init__(self, status_code: int = 500, detail: str = ""):
        self.status_code = status_code
        self.detail = detail


class Request:
    def __init__(self):
        self.method = ""
        self.url = ""
        self.headers = {}


class Response:
    def __init__(self, content="", status_code: int = 200):
        self.body = content
        self.status_code = status_code


class FastAPI:
    def __init__(self, title: str = "", version: str = "0.1.0"):
        self.title = title
        self.version = version

    def get(self, path: str, **kwargs):
        def decorator(func):
            return func
        return decorator

    def post(self, path: str, **kwargs):
        def decorator(func):
            return func
        return decorator

    def put(self, path: str, **kwargs):
        def decorator(func):
            return func
        return decorator

    def delete(self, path: str, **kwargs):
        def decorator(func):
            return func
        return decorator

    def patch(self, path: str, **kwargs):
        def decorator(func):
            return func
        return decorator

    def middleware(self, kind: str):
        def decorator(func):
            return func
        return decorator

    def exception_handler(self, exc_class):
        def decorator(func):
            return func
        return decorator

    def include_router(self, router, prefix: str = "") -> None:
        return None


class APIRouter:
    def __init__(self, prefix: str = ""):
        self.prefix = prefix

    def get(self, path: str, **kwargs):
        def decorator(func):
            return func
        return decorator

    def post(self, path: str, **kwargs):
        def decorator(func):
            return func
        return decorator


def Depends(dependency=None):
    return dependency


def Query(default=None, **kwargs):
    return default


def Path(default=None, **kwargs):
    return default


def Body(default=None, **kwargs):
    return default


def Header(default=None, **kwargs):
    return default


def Cookie(default=None, **kwargs):
    return default


def Form(default=None, **kwargs):
    return default
