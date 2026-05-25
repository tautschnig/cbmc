# PLR §4.6: bytes is a sequence of integers in [0, 256). We
# model it as list[uint8] sharing Python list's struct shape
# so existing list-method handlers work transparently.

def basic_bytes() -> None:
    b: bytes = b"Hello"
    assert b[0] == 72   # 'H'
    assert b[1] == 101  # 'e'
    assert b[2] == 108  # 'l'
    assert b[3] == 108  # 'l'
    assert b[4] == 111  # 'o'


def bytes_param(arg: bytes) -> None:
    assert arg[0] == 72


def bytes_call_chain() -> None:
    b: bytes = b"Hello"
    bytes_param(b)


basic_bytes()
bytes_call_chain()
