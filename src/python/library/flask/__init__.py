"""
Verification model of the third-party `flask` web framework.

Flask is request-driven; for verification we model the
application object, route decorators, and the request
object as opaque. Decorators return the decorated
function unchanged.
"""


class Request:
    def __init__(self):
        self.method = ""
        self.path = ""
        self.args = {}
        self.form = {}
        self.json = None
        self.headers = {}
        self.cookies = {}

    def get_json(self, force: bool = False, silent: bool = False):
        return self.json


class Response:
    def __init__(self, body="", status: int = 200, headers=None):
        self.data = body
        self.status_code = status
        self.headers = headers if headers is not None else {}


class Flask:
    def __init__(self, name: str):
        self.name = name
        self.config = {}

    def route(self, rule: str, **kwargs):
        def decorator(func):
            return func
        return decorator

    def get(self, rule: str, **kwargs):
        def decorator(func):
            return func
        return decorator

    def post(self, rule: str, **kwargs):
        def decorator(func):
            return func
        return decorator

    def run(self, host: str = "127.0.0.1", port: int = 5000,
            debug: bool = False) -> None:
        return None

    def test_client(self):
        return self


def jsonify(*args, **kwargs) -> Response:
    return Response("", 200)


def redirect(location: str, code: int = 302) -> Response:
    return Response("", code)


def abort(code: int, description: str = "") -> None:
    raise RuntimeError("abort")


def url_for(endpoint: str, **values) -> str:
    return ""


def render_template(template_name: str, **context) -> str:
    return ""


request = Request()


class Blueprint:
    def __init__(self, name: str, import_name: str):
        self.name = name

    def route(self, rule: str, **kwargs):
        def decorator(func):
            return func
        return decorator
