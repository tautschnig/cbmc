#!/usr/bin/env python3
"""CBMC Python parse daemon.

Long-running Unix-domain socket server that parses Python source
files into the same JSON AST format as PYTHON_AST_TO_JSON_CODE in
src/python/python_language.cpp. Each request is one parse; the
Python interpreter, the `ast` module, and the `json` module stay
loaded across requests, eliminating the ~28 ms python3 startup
that currently dominates short-benchmark wall time.

We deliberately do NOT use mypy or any other type-inference
machinery — only Python's stdlib `ast.parse`. CBMC's Python
front-end already has its own type model and treats stub-derived
type annotations as untrusted hints; we want only the syntactic
parse here.

Protocol (mirrors src/typescript/ts_ast_server.js):

  Client -> Server: <absolute-source-path>\\n
  Server -> Client: <json-byte-length>\\n<json-payload>\\n

On parse error the server returns a JSON envelope:

  {"_type": "Error", "message": "<what went wrong>",
   "lineno": <int>, "offset": <int>}

The CBMC front-end recognises the envelope and surfaces it as a
parse error.

Lifecycle: managed by scripts/cbmc_python_server. Daemon prints
"READY <socket-path>" to stdout on startup so the wrapper can
detect successful initialisation. Graceful shutdown on SIGTERM /
SIGINT removes the socket.
"""

import ast
import json
import os
import signal
import socket
import sys
import threading


def _json_default(o):
    """JSON serializer for non-serializable values.

    Python complex literals (e.g. `2j`, `1+2j`) appear as a complex
    value on the AST's Constant.value field. Without this hook,
    json.dumps fails. Returning str(o) is wrong for the complex case
    because the CBMC frontend's Constant-string path would parse a
    `"2j"` source-string literal the same as a `2j` imaginary
    literal — yet Python distinguishes the two: `"2j" + "x"` is a
    string concat, `2j + 1` is complex addition.

    We emit complex literals as a tagged dict so the frontend can
    distinguish them from plain strings.
    """
    if isinstance(o, complex):
        return {"__complex__": True, "real": o.real, "imag": o.imag}
    return str(o)


# Convert a Python AST node to the JSON shape CBMC expects.
def to_dict(node):
    if isinstance(node, ast.AST):
        d = {"_type": node.__class__.__name__}
        for field, value in ast.iter_fields(node):
            d[field] = to_dict(value)
        for attr in ("lineno", "col_offset", "end_lineno", "end_col_offset"):
            v = getattr(node, attr, None)
            if v is not None:
                d[attr] = v
        return d
    if isinstance(node, list):
        return [to_dict(x) for x in node]
    return node


def _compile_only_syntax_error(src, path):
    """Return an Error envelope for the precise set of genuine SyntaxErrors
    that `ast.parse` accepts but the compiler rejects, or None. Restricted
    to an allow-list so the frontend keeps tolerating constructs it models
    leniently (top-level await, break/return in except*)."""
    _ALLOW = (
        "prior to global declaration",
        "prior to nonlocal declaration",
        "keyword argument repeated",
        "duplicate argument",
    )
    try:
        compile(src, path, "exec")
    except SyntaxError as e:
        msg = str(e)
        if any(frag in msg for frag in _ALLOW):
            return {
                "_type": "Error",
                "message": msg,
                "lineno": getattr(e, "lineno", 0) or 0,
                "offset": getattr(e, "offset", 0) or 0,
            }
    except Exception:
        pass
    return None


def parse_file(path):
    """Parse a single source file. Returns the JSON-serialisable AST or
    an error envelope."""
    try:
        with open(path, "r", encoding="utf-8") as f:
            src = f.read()
    except OSError as e:
        return {"_type": "Error", "message": f"cannot read {path}: {e}"}
    try:
        tree = ast.parse(src, path)
    except SyntaxError as e:
        return {
            "_type": "Error",
            "message": str(e),
            "lineno": e.lineno or 0,
            "offset": e.offset or 0,
        }
    # Some genuine SyntaxErrors are only raised by the compiler, not by
    # ast.parse (PLR §4.2.4 / §8): a name used prior to its `global` /
    # `nonlocal` declaration, and a repeated keyword argument. Surface
    # ONLY those (a precise allow-list) so CBMC rejects the invalid program
    # instead of silently verifying it. Other compile-only errors that the
    # frontend deliberately tolerates (top-level `await`, `break`/`return`
    # in `except*`) are intentionally NOT surfaced here.
    se = _compile_only_syntax_error(src, path)
    if se is not None:
        return se
    out = to_dict(tree)
    out["_filename"] = path
    # Over-inclusive set of every name bound ANYWHERE in the module
    # (all scopes, all binding forms). The C++ frontend gates its
    # NameError-on-undefined-name check on ABSENCE from this set, so
    # over-inclusion is the safe direction: it can only miss a NameError,
    # never invent one. Authoritative because it uses Python's own ast
    # (Store-context Names cover assignment / tuple-or-list unpack /
    # for-targets / with-as / comprehension targets / walrus uniformly).
    out["_all_bound_names"] = sorted(_collect_bound_names(tree))
    out["_python_version"] = [sys.version_info[0], sys.version_info[1]]
    return out


def _collect_bound_names(tree):
    """All names bound anywhere in `tree`, across every scope and binding
    form (PLR §4.2.1). Used as a conservative 'is this name bound at all?'
    oracle for NameError detection.

    Comprehension/generator-expression TARGET occurrences do not count:
    PLR §6.2.4 gives each comprehension "a separate implicitly nested
    scope", so its targets are invisible to all surrounding code (reading
    one outside is a NameError CPython actually raises). A name that is
    additionally bound by any non-comprehension binder still counts, and
    a walrus inside a comprehension binds in the enclosing scope
    (PEP 572), counting through its own Store occurrence."""
    names = set()

    comp_types = (ast.ListComp, ast.SetComp, ast.DictComp, ast.GeneratorExp)
    comp_target_nodes = set()
    for n in ast.walk(tree):
        if isinstance(n, comp_types):
            for gen in n.generators:
                for t in ast.walk(gen.target):
                    if isinstance(t, ast.Name):
                        comp_target_nodes.add(id(t))

    class _V(ast.NodeVisitor):
        def visit_Name(self, node):
            if isinstance(node.ctx, ast.Store) and \
                    id(node) not in comp_target_nodes:
                names.add(node.id)
            self.generic_visit(node)

        def visit_arg(self, node):
            names.add(node.arg)
            self.generic_visit(node)

        def visit_FunctionDef(self, node):
            names.add(node.name)
            self.generic_visit(node)

        visit_AsyncFunctionDef = visit_FunctionDef

        def visit_ClassDef(self, node):
            names.add(node.name)
            self.generic_visit(node)

        def visit_Import(self, node):
            for a in node.names:
                names.add((a.asname or a.name).split(".")[0])

        def visit_ImportFrom(self, node):
            for a in node.names:
                if a.name != "*":
                    names.add(a.asname or a.name)

        def visit_Global(self, node):
            names.update(node.names)

        def visit_Nonlocal(self, node):
            names.update(node.names)

        def visit_ExceptHandler(self, node):
            if node.name:
                names.add(node.name)
            self.generic_visit(node)

    _V().visit(tree)
    return names


def serve_one(conn):
    """Handle one connection: read a path, write back JSON.

    Connection is closed after the single round-trip (matches the
    TS server's framing).
    """
    try:
        # Read the path until '\n'.
        buf = bytearray()
        while True:
            chunk = conn.recv(4096)
            if not chunk:
                return
            buf.extend(chunk)
            nl = buf.find(b"\n")
            if nl >= 0:
                path = buf[:nl].decode("utf-8", errors="replace")
                break
        result = parse_file(path)
        payload = json.dumps(
          result, default=_json_default, ensure_ascii=False).encode(
            "utf-8")
        header = f"{len(payload)}\n".encode("ascii")
        conn.sendall(header)
        conn.sendall(payload)
        conn.sendall(b"\n")
    except Exception as e:
        # Best-effort: try to send an error envelope so the client
        # gets a structured reply instead of a half-finished payload.
        try:
            err = json.dumps({"_type": "Error", "message": f"daemon error: {e}"})
            conn.sendall(f"{len(err)}\n".encode("ascii"))
            conn.sendall(err.encode("utf-8"))
            conn.sendall(b"\n")
        except Exception:
            pass


def main():
    if len(sys.argv) < 2:
        print(
            "usage: python_ast_server.py <socket-path>", file=sys.stderr)
        sys.exit(2)
    sock_path = sys.argv[1]

    # Stale-socket cleanup.
    if os.path.exists(sock_path):
        os.unlink(sock_path)

    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(sock_path)
    server.listen(64)

    # Restrict permissions to the owner.
    os.chmod(sock_path, 0o600)

    # Print READY for the wrapper to detect.
    print(f"READY {sock_path}", flush=True)

    stop = threading.Event()

    def shutdown(signum, frame):
        stop.set()
        # Close the listening socket so accept() returns.
        try:
            server.shutdown(socket.SHUT_RDWR)
        except Exception:
            pass
        try:
            server.close()
        except Exception:
            pass
        try:
            if os.path.exists(sock_path):
                os.unlink(sock_path)
        except Exception:
            pass

    signal.signal(signal.SIGTERM, shutdown)
    signal.signal(signal.SIGINT, shutdown)

    while not stop.is_set():
        try:
            conn, _ = server.accept()
        except OSError:
            break
        # Handle in a worker thread so a slow client (or large file
        # being read off disk) doesn't block other parses.
        t = threading.Thread(target=serve_one, args=(conn,), daemon=True)
        t.start()
        # Per-connection close happens inside serve_one's try/except;
        # ensure conn is closed regardless.
        threading.Thread(
            target=lambda c=conn: (t.join(), c.close()), daemon=True).start()


if __name__ == "__main__":
    main()
