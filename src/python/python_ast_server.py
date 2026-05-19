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
    out = to_dict(tree)
    out["_filename"] = path
    return out


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
        payload = json.dumps(result, default=str, ensure_ascii=False).encode(
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
