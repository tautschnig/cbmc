// Package regexp, modelled by its own source (plan §4.5); \p{...} classes
// need Unicode tables, which are not modelled, and are reported.
package main

import (
	"cprover"
	"regexp"
)

func main() {
	re := regexp.MustCompile(`^a+b$`)
	cprover.Assert(re.MatchString("aab"), "a one-pass regexp matches")
	cprover.Assert(!re.MatchString("ba"), "and does not match")

	kv := regexp.MustCompile(`(\w+)=(\d+)`)
	m := kv.FindStringSubmatch("x k=42;")
	cprover.Assert(len(m) == 3 && m[1] == "k" && m[2] == "42",
		"submatches with the backtracker")
	cprover.Assert(kv.NumSubexp() == 2, "NumSubexp")

	_, err := regexp.Compile(`a(`)
	cprover.Assert(err != nil, "a syntax error")
	cprover.Assert(regexp.QuoteMeta("a.b") == `a\.b`, "QuoteMeta")
}
