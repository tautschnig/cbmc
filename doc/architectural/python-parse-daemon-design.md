\file
# Python parse daemon — design and measured impact

## Motivation

Profiling on the AWS Python benchmark suite showed that
short-running benchmarks spend a large fraction of their wall
time in `python3` subprocess startup. Per `perf record`,
`ecs_utils` (3.4 s wall under cvc5 after the
`irept::compare` SHARING fast-path) spends 37 % in
`_PyEval_EvalFrameDefault` — i.e., the Python interpreter
itself parsing stub modules.

CBMC's Python front-end currently shells out to
`python3 -c '<inline script>' <input.py> <output.json>`
once per top-level source AND once per imported module.
Each invocation pays a ~28 ms cold-start cost (interpreter
init + `import ast, json`). For boto3-heavy benchmarks with
30+ imports per run, that is ~1 s of pure subprocess overhead.

The TypeScript front-end faced the same issue and addressed
it via a parse daemon
(commits 6d6c03bf59638ccc58aec959a85c5cb1505c840b and
6328bebfa4dec6f00866515f718869517a02bcf4 on the
`tautschnig/ts` branch): a long-running Node.js process that
keeps a TypeScript Language Service alive across CBMC
invocations, communicating over a Unix-domain socket. That
gave 18× speedup on the 629-test TS regression suite.

This document describes the equivalent design for Python.

## Why not mypy daemon?

mypy ships its own daemon (`dmypy`, see
<https://mypy.readthedocs.io/en/stable/mypy_daemon.html>). It
is **not** suitable for our purposes:

* We want only the syntactic AST, not type inference. CBMC's
  Python front-end already has its own type model (PLR §3.2
  with bounded approximations) and treats imported-stub type
  annotations as untrusted hints. mypy would do work we don't
  want.
* mypy's output is not the `ast.dump`-shaped JSON CBMC
  consumes today; bridging would require a translation layer.
* mypy has its own caching strategy (`.mypy_cache/`) that we
  would need to either trust or disable, complicating the
  trust boundary.

The simpler approach: keep `ast.parse` exactly as today,
just hold a Python interpreter open across invocations and
serve parse requests over a socket.

## Design

Mirrors the TypeScript daemon design closely; the protocol
and lifecycle commands are intentionally aligned.

### Server (`src/python/python_ast_server.py`)

* Plain Python script, no third-party dependencies (only the
  stdlib `ast`, `json`, `socket`, `signal`, `threading`).
* Listens on a Unix-domain socket whose path is given as the
  first argument.
* Per request:
  1. Read `<absolute-source-path>\n` from the socket.
  2. Parse with `ast.parse(open(path).read(), path)`.
  3. Walk the AST into the same dict shape as the existing
     `PYTHON_AST_TO_JSON_CODE` inline script in
     `src/python/python_language.cpp`.
  4. Serialise to JSON.
  5. Send `<json-byte-length>\n<json-payload>\n` back.
* On `SyntaxError`, returns an envelope:
  `{"_type": "Error", "message": "...", "lineno": N, "offset": M}`.
  The client treats this as a parse failure and falls through
  to the one-shot path so the user sees the same error
  message as today.
* Each connection is handled on its own worker thread so a
  slow stub-file read doesn't block other parses.
* Graceful shutdown on `SIGTERM` / `SIGINT` removes the
  socket file.

### Lifecycle wrapper (`scripts/cbmc_python_server`)

Bash script aligned with `scripts/cbmc_ts_server`:

```
cbmc_python_server {start|stop|status|socket-path}
```

* `start` launches the server (via `nohup python3 …`),
  waits for `READY` to appear in the log file (10 s timeout),
  prints the socket path on stdout.
* `stop` sends SIGTERM, waits up to 3 s, falls back to
  SIGKILL.
* PID file at `${XDG_RUNTIME_DIR:-/tmp}/cbmc-python-$USER.pid`,
  socket at `…/cbmc-python-$USER.sock`, log at `…/cbmc-python-$USER.log`.
* The socket has `0o600` permissions (owner-only read/write).

### Client (`src/python/python_language.cpp`)

* New static helper `python_parse_via_daemon(path,
  json_path, message_handler)`. If the env var
  `CBMC_PYTHON_SERVER_SOCKET` is set and the socket is
  reachable:
  1. Resolve the source path to an absolute path.
  2. Send `<absolute-path>\n` to the daemon.
  3. Read header `<length>\n` and payload of that length.
  4. Write the payload to `json_path` (so the rest of
     `parse()` sees the exact same `.json` file the
     subprocess path would have produced).
* On any error (env var unset, socket unreachable, bad
  framing, error envelope, etc.) the helper returns `false`
  and the existing `run("python3", …)` subprocess path
  executes — daemon is purely an optimisation, never a
  soundness or behavioural change.
* Two call sites: top-level `parse()` (for the input file)
  and `resolve_module()` (for each imported module).
* Per-process logic: the same daemon connection isn't reused
  across requests within the same CBMC run, but each request
  is one `connect()` + one round-trip, with the daemon
  process kept warm across runs.

## Measured impact

`ecs_utils.py` under `--smt2 --cvc5` (suite outlier where
parse cost dominates after the `irept::compare` fast-path):

```
Without daemon: 3.6 s wall, 35 execve("python3", …) calls
With daemon   : 2.3 s wall, 0 such calls (daemon is reused)

           Δ : −36 %
```

Suite-wide on the 51-benchmark AWS suite under
`--smt2 --cvc5 --object-bits 12` (8 GB ulimit):

| | wall (sum) | wall (median) | wall (max) |
|---|---:|---:|---:|
| Without daemon | 338 s | 3.0 s | 64.5 s |
| With daemon    | 273 s | 1.7 s | 62.9 s |
| **Δ**          | **−19 %** | **−43 %** | **−2 %** |

The median moves the most because *short* benchmarks were
parse-bound; the *max* barely moves because the heaviest
outliers (`s3_backup_restore`, `test_bedrock_guardrails`) are
solver-bound. RSS is unchanged because we removed temporary
processes, not allocations within CBMC itself.

Combined with `--slice-formula` (which also helps cvc5 on
those solver-bound outliers) the cvc5 suite total drops to
under 200 s. See `python-perf-analysis.md` for the
`--slice-formula` measurements.

## Trust boundary

* The daemon does not see any data the existing one-shot
  subprocess wouldn't see.
* The daemon parses the same source files the user passes
  to CBMC (or that CBMC's import resolver decides to load).
* The daemon writes the same JSON shape.
* No mypy. No type inference. No `.mypy_cache/`.
* No type annotations are taken on faith — CBMC's Python
  front-end ignores everything beyond what `ast.parse`
  produces.
* The socket has owner-only permissions.
* If the daemon is unreachable or returns an error envelope,
  CBMC falls back to the existing one-shot path. There is no
  way for daemon misbehaviour to change verification
  outcomes.

## Use

Off by default. Two ways to enable:

```bash
# Manual
$ eval $(scripts/cbmc_python_server start)         # prints + sets nothing yet
$ export CBMC_PYTHON_SERVER_SOCKET=$(scripts/cbmc_python_server socket-path)
$ cbmc your_file.py [...]
$ scripts/cbmc_python_server stop

# Or in one go:
$ export CBMC_PYTHON_SERVER_SOCKET=$(scripts/cbmc_python_server start)
$ cbmc your_file.py [...]
$ scripts/cbmc_python_server stop
```

The daemon is fully optional and stateless across runs (no
disk cache, only in-memory interpreter state). When the
daemon is not running, behaviour is identical to today's
one-shot mode.

## Future work

* CMake fixture (`FIXTURES_SETUP` / `FIXTURES_CLEANUP`) so
  `ctest -R python-` picks up the daemon automatically. The
  TS branch's `regression/typescript/CMakeLists.txt` is the
  template.
* In-memory parse-result cache keyed on file content/mtime
  so repeated parses of the same stub file (e.g. across
  different top-level benchmarks in the same `ctest` run)
  return without re-parsing. The TS daemon explicitly
  *clears* its files map between parses to avoid quadratic
  growth in the Language Service's checker, but Python's
  `ast.parse` has no such hazard, and the cache hit on
  `boto3/__init__.py` would be substantial.
* Multi-process pool for parallel parse requests — currently
  serialised through one Python interpreter. Useful only if
  CBMC starts issuing parse requests in parallel.
