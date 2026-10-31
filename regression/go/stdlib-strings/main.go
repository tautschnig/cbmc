// Calls of standard-library functions use models of them
// (src/go/helper/models), whose results are those the package documents.
// The declarations still come from the real package (Go1.27
// §Import_declarations), so programs type-check as with gc.
package main

import (
	"cprover"
	"strings"
)

func main() {
	cprover.Assert(strings.HasPrefix("golang", "go") &&
		!strings.HasSuffix("golang", "go"), "prefix, suffix")
	cprover.Assert(strings.Index("chicken", "ken") == 4 &&
		strings.LastIndex("go gopher", "go") == 3, "index")
	cprover.Assert(strings.Count("cheese", "e") == 3 &&
		strings.Count("five", "") == 5, "count")
	parts := strings.Split("a,b,c", ",")
	cprover.Assert(len(parts) == 3 && parts[2] == "c" && cap(parts) == 3,
		"split")
	cprover.Assert(strings.Join(parts, "-") == "a-b-c", "join")
	cprover.Assert(strings.Repeat("ab", 2) == "abab", "repeat")
	cprover.Assert(strings.TrimSpace("\u00a0 x y\t") == "x y", "trim space")
	fields := strings.Fields(" a  b ")
	cprover.Assert(len(fields) == 2 && fields[1] == "b", "fields")
	k, v, found := strings.Cut("k=v", "=")
	cprover.Assert(found && k == "k" && v == "v", "cut")
	cprover.Assert(strings.Compare("a", "b") == -1, "compare")

	// For all strings of up to three bytes.
	s := cprover.Nondet[string]()
	cprover.Assume(len(s) <= 3)
	if strings.HasPrefix(s, "ab") {
		cprover.Assert(s[0] == 'a' && strings.Index(s, "b") == 1, "prefix")
	}
	if i := strings.Index(s, "x"); i >= 0 {
		cprover.Assert(s[i] == 'x' && strings.Contains(s, "x"), "index")
	}
	cprover.Assert(strings.TrimPrefix("p"+s, "p") == s, "trim prefix")
}
