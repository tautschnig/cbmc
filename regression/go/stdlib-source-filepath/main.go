// Packages path/filepath and io/fs, modelled by their own source (plan
// §4.5); the functions that use the file system (Walk, Glob, Abs, ...)
// call package os, which has no model, and are reported when called.
package main

import (
	"cprover"
	"io/fs"
	"path/filepath"
)

func main() {
	cprover.Assert(filepath.Join("a", "b/../c", "/d") == "a/c/d", "Join")
	cprover.Assert(filepath.Base("/x/y.go") == "y.go", "Base")
	cprover.Assert(filepath.Ext("y.go") == ".go", "Ext")
	cprover.Assert(filepath.Dir("/x/y") == "/x", "Dir")
	ok, err := filepath.Match("*.go", "y.go")
	cprover.Assert(ok && err == nil, "Match")
	rel, err := filepath.Rel("/a/b", "/a/c/d")
	cprover.Assert(err == nil && rel == "../c/d", "Rel")
	cprover.Assert(filepath.IsAbs("/a") && filepath.ToSlash("a/b") == "a/b",
		"IsAbs, ToSlash")
	cprover.Assert(len(filepath.SplitList("a:b")) == 2, "SplitList")
	cprover.Assert(fs.ValidPath("a/b") && !fs.ValidPath("../a"), "ValidPath")
}
