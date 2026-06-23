"""
Verification model of the `subprocess` module.

Process spawning is not modelled. Calls return nondet or
conventional default structures; no actual subprocess is
spawned. Suitable for code that only exercises the API
surface (argument parsing, error handling) at verification
time.
"""


# Exit codes: 0 success; non-zero indicates failure.
# We use -1 as a sentinel for 'unknown' return code.


class SubprocessError(Exception):
    pass


class TimeoutExpired(SubprocessError):
    def __init__(self, cmd=None, timeout: float = 0.0, output=None,
                 stderr=None):
        self.cmd = cmd
        self.timeout = timeout
        self.output = output
        self.stderr = stderr


class CalledProcessError(SubprocessError):
    def __init__(self, returncode: int, cmd=None, output=None, stderr=None):
        self.returncode = returncode
        self.cmd = cmd
        self.output = output
        self.stderr = stderr


PIPE = -1
STDOUT = -2
DEVNULL = -3


class CompletedProcess:
    def __init__(self, args=None, returncode: int = 0, stdout=None, stderr=None):
        self.args = args
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr

    def check_returncode(self) -> None:
        if self.returncode != 0:
            raise CalledProcessError(self.returncode, self.args)


class Popen:
    def __init__(self, args=None, bufsize: int = -1, executable=None,
                 stdin=None, stdout=None, stderr=None,
                 preexec_fn=None, close_fds: bool = True, shell: bool = False,
                 cwd=None, env=None, universal_newlines=None, startupinfo=None,
                 creationflags: int = 0, restore_signals: bool = True,
                 start_new_session: bool = False, pass_fds=(),
                 encoding=None, errors=None, text=None):
        self.args = args
        self.returncode = None
        self.pid = 0
        self.stdin = None
        self.stdout = None
        self.stderr = None

    def communicate(self, input=None, timeout=None):
        return (b"", b"")

    def wait(self, timeout=None) -> int:
        self.returncode = 0
        return 0

    def poll(self):
        return self.returncode

    def kill(self) -> None:
        return None

    def terminate(self) -> None:
        return None

    def send_signal(self, sig) -> None:
        return None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        return False


def run(args=None, *, stdin=None, input=None, stdout=None, stderr=None,
        capture_output: bool = False, shell: bool = False,
        cwd=None, timeout=None, check: bool = False, encoding=None,
        errors=None, text=None, env=None, universal_newlines=None,
        **other) -> CompletedProcess:
    return CompletedProcess(args=args, returncode=0, stdout=b"", stderr=b"")


def call(args=None, *, stdin=None, stdout=None, stderr=None,
         shell: bool = False, cwd=None, timeout=None) -> int:
    return 0


def check_call(args=None, *, stdin=None, stdout=None, stderr=None,
               shell: bool = False, cwd=None, timeout=None) -> int:
    return 0


def check_output(args=None, *, stdin=None, stderr=None,
                 shell: bool = False, cwd=None, timeout=None, input=None,
                 encoding=None, errors=None, text=None, **other) -> bytes:
    return b""


def getoutput(cmd) -> str:
    return nondet_str()


def getstatusoutput(cmd):
    return (0, "")
