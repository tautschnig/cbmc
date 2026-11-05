// Package path, modelled by its own source (plan §4.5).
package main

import (
	"cprover"
	"path"
)

func main() {
	cprover.Assert(path.Join("a", "b/../c", "/d") == "a/c/d", "Join cleans")
	cprover.Assert(path.Base("/x/y.go") == "y.go", "Base")
	cprover.Assert(path.Ext("y.go") == ".go", "Ext")
	cprover.Assert(path.Dir("/x/y") == "/x", "Dir")
	ok, err := path.Match("*.go", "y.go")
	cprover.Assert(ok && err == nil, "Match")
	_, err = path.Match("[", "x")
	cprover.Assert(err == path.ErrBadPattern, "a malformed pattern")
	cprover.Assert(path.IsAbs("/a") && !path.IsAbs("a"), "IsAbs")
	cprover.Assert(path.Clean("a//b/./") == "a/b", "Clean")
	dir, file := path.Split("a/b/c.txt")
	cprover.Assert(dir == "a/b/" && file == "c.txt", "Split")
}
