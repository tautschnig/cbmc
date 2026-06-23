import io
buf = io.BytesIO(b"hello")
# was a false proof (read returned b""); real content is b"hello"
assert buf.read() == b""
