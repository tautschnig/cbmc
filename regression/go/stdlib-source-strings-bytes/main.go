// Packages strings, bytes and unicode/utf8 are their real source (with
// some functions of strings replaced), and unicode is exact for Latin-1:
// strings.Builder, bytes.Buffer and the functions of the packages.
package main

import (
	"bytes"
	"cprover"
	"strings"
	"unicode"
)

func main() {
	var b strings.Builder
	b.WriteString("ab")
	b.WriteByte('c')
	b.WriteRune('d')
	cprover.Assert(b.String() == "abcd" && b.Len() == 4, "Builder")
	cprover.Assert(strings.ToUpper("aB") == "AB", "ToUpper")
	cprover.Assert(strings.EqualFold("Go", "GO"), "EqualFold")
	cprover.Assert(strings.TrimLeft("xxa", "x") == "a", "TrimLeft")
	parts := strings.Split("a,b", ",")
	cprover.Assert(len(parts) == 2 && parts[1] == "b", "Split")
	cprover.Assert(strings.Repeat("ab", 2) == "abab", "Repeat")

	var buf bytes.Buffer
	buf.WriteString("hi")
	buf.WriteByte('!')
	cprover.Assert(buf.String() == "hi!" && buf.Len() == 3, "Buffer")
	c, _ := buf.ReadByte()
	cprover.Assert(c == 'h' && buf.String() == "i!", "ReadByte")
	cprover.Assert(bytes.Equal([]byte("x"), []byte("x")), "Equal")
	cprover.Assert(bytes.HasPrefix([]byte("xy"), []byte("x")), "HasPrefix")

	// Function values of other packages.
	upper := strings.ToUpper
	cprover.Assert(upper("go") == "GO", "function value")
	cprover.Assert(strings.Map(unicode.ToLower, "Go") == "go", "Map")
	cprover.Assert(unicode.IsSpace(0x3000) && !unicode.IsSpace('x') &&
		unicode.IsUpper('Q') && unicode.IsLetter(0xE9) && !unicode.IsDigit('a'),
		"unicode")
}
