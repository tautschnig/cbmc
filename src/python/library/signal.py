"""
Verification model of the `signal` module.

Provides the signal number constants; signal-handler installation
returns a nondet stand-in since verification is not typically run
against signal delivery semantics.
"""

SIGABRT: int = 6
SIGALRM: int = 14
SIGBUS: int = 7
SIGCHLD: int = 17
SIGCLD: int = 17
SIGCONT: int = 18
SIGFPE: int = 8
SIGHUP: int = 1
SIGILL: int = 4
SIGINT: int = 2
SIGIO: int = 29
SIGIOT: int = 6
SIGKILL: int = 9
SIGPIPE: int = 13
SIGPOLL: int = 29
SIGPROF: int = 27
SIGPWR: int = 30
SIGQUIT: int = 3
SIGRTMAX: int = 64
SIGRTMIN: int = 34
SIGSEGV: int = 11
SIGSTKFLT: int = 16
SIGSTOP: int = 19
SIGSYS: int = 31
SIGTERM: int = 15
SIGTRAP: int = 5
SIGTSTP: int = 20
SIGTTIN: int = 21
SIGTTOU: int = 22
SIGURG: int = 23
SIGUSR1: int = 10
SIGUSR2: int = 12
SIGVTALRM: int = 26
SIGWINCH: int = 28
SIGXCPU: int = 24
SIGXFSZ: int = 25
SIG_BLOCK: int = 0
SIG_DFL: int = 0
SIG_IGN: int = 1
SIG_SETMASK: int = 2
SIG_UNBLOCK: int = 1

SIG_DFL: int = 0
SIG_IGN: int = 1
NSIG: int = 65

def signal(signalnum: int, handler) -> int:
    return 0

def getsignal(signalnum: int):
    return None

def raise_signal(signalnum: int) -> None:
    return None

def pause() -> None:
    return None

def alarm(time: int) -> int:
    return 0
