// Go1.27 §Handling_panics: "Any functions deferred by F are then executed
// as usual. Next, any deferred functions run by F's caller are run, and so
// on up to any deferred by the top-level function in the executing
// goroutine. At that point, the program is terminated and the error
// condition is reported". Deferred calls run (and their assertions are
// checked) while panicking; the panic is reported at its site.
package main

import "cprover"

var cleaned bool

func work(p *int) int {
	defer func() { cleaned = true }()
	return *p
}

func main() {
	defer func() {
		cprover.Assert(cleaned, "deferred calls of callees ran first")
		cprover.Assert(false, "reached while panicking")
	}()
	var p *int
	if cprover.Nondet[bool]() {
		p = new(int)
	}
	work(p)
	cprover.Assert(p != nil, "only reached without a panic")
}
