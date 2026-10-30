// Go1.27 §Conversions_to_and_from_a_string_type, with the spec's examples:
// "Converting a slice of bytes to a string type yields a string whose
// successive bytes are the elements of the slice." "Converting a value of
// a string type to a slice of bytes type yields a non-nil slice whose
// successive elements are the bytes of the string." Runes are converted
// to and from UTF-8; "Values outside the range of valid Unicode code
// points are converted to "\uFFFD"."
package main

import "cprover"

type myByte byte

func main() {
	cprover.Assert(string([]byte{'h', 'e', 'l', 'l', '\xc3', '\xb8'}) == "hellø",
		"bytes to string")
	cprover.Assert(string([]byte{}) == "" && string([]byte(nil)) == "",
		"empty and nil")
	cprover.Assert(string([]myByte{'w', 'o', 'r', 'l', 'd', '!'}) == "world!",
		"named byte type")

	b := []byte("hellø")
	cprover.Assert(len(b) == 6 && b[4] == '\xc3' && b[5] == '\xb8', "string to bytes")
	cprover.Assert([]byte("") != nil, "a non-nil slice")
	s := "abc"
	c := []byte(s)
	c[0] = 'X'
	cprover.Assert(s == "abc" && string(c) == "Xbc", "strings are immutable")

	r := []rune("白鵬翔")
	cprover.Assert(len(r) == 3 && r[0] == 0x767d && r[2] == 0x7fd4, "string to runes")
	cprover.Assert(string([]rune{0x767d, 0x9d6c, 0x7fd4}) == "白鵬翔", "runes to string")

	cprover.Assert(string(rune(0x65e5)) == "日", "rune to string")
	cprover.Assert(string(rune(-1)) == "\ufffd", "negative")
	var big int64 = 0x110000
	cprover.Assert(string(rune(big)) == "\ufffd", "above 0x10FFFF")
	cprover.Assert(string(rune(0xD800)) == "\ufffd", "surrogate")
}
