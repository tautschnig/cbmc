# §0 write-through, KEY form: a STRING-keyed extraction folds to the
# tracked literal's value for read precision (dict18/19), so no slot
# expression exists at extraction. The alias records the constant KEY
# instead, and the write-through emits a key-match store (the key chain
# reads PRE-statement keys -- a value mutation never changes the key
# array; structural changes demote). Extraction-then-mutate is now
# precise for string keys too; hazards (same-key overwrite, new-key
# insert) demote to the sound havoc exactly like the slot form.
d = {"a": [1], "b": [5]}
v = d["a"]
v.append(2)
assert len(d["a"]) == 2
assert len(d["b"]) == 1
assert d["a"][0] == 1
assert d["b"][0] == 5
