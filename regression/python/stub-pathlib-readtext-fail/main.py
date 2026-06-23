import pathlib
assert pathlib.Path("/a").read_text() == ""
