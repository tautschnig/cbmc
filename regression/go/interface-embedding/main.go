// Go1.27 §Embedded_interfaces: "The type set of T is the intersection of the
// type sets defined by T's explicitly declared methods and the type sets of
// T’s embedded interfaces." A struct embedding an interface gets its methods
// promoted (§Struct_types).
package main

import "cprover"

type Reader interface{ Read() int }
type Closer interface{ Close() bool }
type ReadCloser interface {
	Reader
	Closer
}

type File struct{ data int }

func (f File) Read() int   { return f.data }
func (f File) Close() bool { return true }

type Wrapper struct {
	Reader // methods of the dynamic value are promoted
	extra  int
}

func main() {
	var rc ReadCloser = File{7}
	cprover.Assert(rc.Read() == 7 && rc.Close(), "embedded interface methods")
	var r Reader = rc // ReadCloser implements Reader
	cprover.Assert(r.Read() == 7, "assignment between interfaces")
	w := Wrapper{File{9}, 1}
	cprover.Assert(w.Read() == 9, "promoted through an embedded interface")
	var r2 Reader = w
	cprover.Assert(r2.Read() == 9, "and through an interface value")
}
