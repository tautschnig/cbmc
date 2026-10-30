// Go1.27 §For_range: "For a string value, the "range" clause iterates over
// the Unicode code points in the string starting at byte index 0. On
// successive iterations, the index value will be the index of the first
// byte of successive UTF-8-encoded code points in the string, and the
// second value, of type rune, will be the value of the corresponding code
// point. If the iteration encounters an invalid UTF-8 sequence, the second
// value will be 0xFFFD, the Unicode replacement character, and the next
// iteration will advance a single byte in the string."
package main

import "cprover"

func main() {
	var idx [8]int
	var runes [8]rune
	n := 0
	for i, r := range "aé☺😀" {
		idx[n], runes[n] = i, r
		n++
	}
	cprover.Assert(n == 4, "four code points")
	cprover.Assert(idx[0] == 0 && idx[1] == 1 && idx[2] == 3 && idx[3] == 6,
		"byte indices")
	cprover.Assert(runes[0] == 'a' && runes[1] == 'é' && runes[2] == '☺' &&
		runes[3] == '😀', "code points")

	n = 0
	for i, r := range "a\xffb\xe2\x98" { // invalid and truncated sequences
		idx[n], runes[n] = i, r
		n++
	}
	cprover.Assert(n == 5, "invalid bytes advance one byte")
	cprover.Assert(runes[1] == 0xFFFD && idx[2] == 2 && runes[2] == 'b' &&
		runes[3] == 0xFFFD && runes[4] == 0xFFFD, "replacement characters")

	n = 0
	for range "\xed\xa0\x80" { // an encoded surrogate is invalid
		n++
	}
	cprover.Assert(n == 3, "surrogates are invalid UTF-8")
}
