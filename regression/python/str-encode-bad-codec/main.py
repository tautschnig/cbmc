# PLR: str.encode with an unknown constant codec name raises LookupError.
b = "abc".encode("not-a-codec")
