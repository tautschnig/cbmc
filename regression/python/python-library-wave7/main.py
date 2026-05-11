# Wave 7 stub smoke test: subprocess, socket, threading,
# asyncio, yaml, requests.

# subprocess
from subprocess import run, CompletedProcess, PIPE
r = run(["echo", "hi"])

# socket
from socket import socket, AF_INET, SOCK_STREAM, gethostname
h = gethostname()
s = socket(AF_INET, SOCK_STREAM)

# threading
from threading import Thread, Lock
t = Thread(target=None)
lk = Lock()

# asyncio
from asyncio import run as arun, get_event_loop, Future
loop = get_event_loop()
fut = Future()

# yaml
from yaml import safe_load, safe_dump, YAMLError
cfg = safe_load("{}")

# requests
from requests import get, Session, Response
sess = Session()
resp = Response()
