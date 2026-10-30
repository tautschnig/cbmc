// Go1.27 §String_types: "A string value is a (possibly empty) sequence of
// bytes. The number of bytes is called the length of the string". "A
// string's bytes can be accessed by integer indices 0 through len(s)-1."
// §String_concatenation: "String addition creates a new string by
// concatenating the operands." §Comparison_operators: "Two string values
// are compared lexically byte-wise." §Slice_expressions: slicing a string
// "constructs a substring".
package main

import "cprover"

func main() {
	s := "héllo" // é is two bytes in UTF-8
	cprover.Assert(len(s) == 6, "length in bytes")
	cprover.Assert(s[0] == 'h' && s[1] == 0xC3 && s[2] == 0xA9, "bytes")

	var zero string
	cprover.Assert(zero == "" && len(zero) == 0, "zero value")

	t := s + ", world"
	cprover.Assert(len(t) == 13 && t[6] == ',', "concatenation")
	cprover.Assert(t[:6] == s && t[8:] == "world", "substrings")
	cprover.Assert(t[:] == t && t[13:] == "", "full and empty substrings")
	t += "!"
	cprover.Assert(len(t) == 14 && t[13] == '!', "+=")

	cprover.Assert("abc" == "abc" && "abc" != "abd", "equality")
	cprover.Assert("abc" < "abd" && "ab" < "abc" && "" < "a", "ordering")
	cprover.Assert("b" > "abc" && "abc" <= "abc" && "abd" >= "abc", "ordering")
	u, v := "x", "y"
	cprover.Assert(u+v == "xy" && v+u == "yx", "non-constant operands")
}
