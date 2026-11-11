# The constant folding of case maps is ASCII only, too: "é".title() is "É".
assert "é".title() == "é"
