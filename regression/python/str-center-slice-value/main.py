# str.center odd padding (extra goes LEFT when marg & width & 1) and
# bounded negative-step slicing (stop is exclusive; the full-reverse
# fold only applies to a bound-less s[::-1]). Both computed the wrong
# value before (ESBMC str_center_odd_padding_fail,
# str_slice_negative_step_fail).
assert "ab".center(7, "-") == "---ab--"
assert "ab".center(6, "-") == "--ab--"
assert "abcdef"[5:0:-1] == "fedcb"
assert "abcdef"[::-1] == "fedcba"
assert "abcdef"[::-2] == "fdb"
assert "abcde"[4:0:-1] == "edcb"
assert "abcdef"[1:4] == "bcd"
