// Models of packages strconv, unicode/utf8, slices (generic functions,
// instantiated as the real ones are) and fmt. The printing functions of
// fmt call the Error and String methods of their arguments; their output
// is not modelled, and their results are arbitrary.
package main

import (
	"cprover"
	"fmt"
	"slices"
	"strconv"
	"unicode/utf8"
)

type named int

var called bool

func (n named) String() string {
	called = true
	return "named"
}

func main() {
	cprover.Assert(strconv.Itoa(-42) == "-42" &&
		strconv.FormatInt(255, 16) == "ff", "strconv")
	cprover.Assert(strconv.FormatBool(true) == "true", "format bool")
	cprover.Assert(utf8.RuneCountInString("h\u00e9") == 2 &&
		utf8.RuneLen('\u00e9') == 2, "rune count")
	r, size := utf8.DecodeRuneInString("\u00e9")
	cprover.Assert(r == '\u00e9' && size == 2, "decode")
	cprover.Assert(utf8.ValidString("ok") && !utf8.ValidString("\xff"),
		"valid")
	xs := []int{3, 1, 2}
	cprover.Assert(slices.Contains(xs, 2) && slices.Index(xs, 1) == 1 &&
		slices.Max(xs) == 3, "slices")
	slices.Reverse(xs)
	cprover.Assert(slices.Equal(xs, []int{2, 1, 3}), "reverse")
	n, err := fmt.Println("x", named(1))
	cprover.Assert(called && n >= 0, "String called")
	_ = err
}
