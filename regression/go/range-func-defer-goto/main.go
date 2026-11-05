// Go1.27 §For_range: the body of a range over a function becomes a yield
// function, but its statements behave as in the enclosing function.
// §Defer_statements: "A "defer" statement invokes a function whose
// execution is deferred to the moment the surrounding function returns",
// in the reverse order they were deferred, so the deferred calls of the
// body run when the function containing the loop returns. §Goto_statements:
// a goto statement may leave the body.
package main

import "cprover"

func seq(n int) func(func(int) bool) {
	return func(yield func(int) bool) {
		for i := 0; i < n; i++ {
			if !yield(i) {
				return
			}
		}
	}
}

var order []int

func deferInBody() (sum int) {
	defer func() { order = append(order, 100) }()
	for i := range seq(3) {
		defer func() { order = append(order, i) }()
		sum += i
	}
	cprover.Assert(len(order) == 0, "nothing runs before the function returns")
	return
}

func deferInNested() {
	for i := range seq(2) {
		for j := range seq(2) {
			defer func() { order = append(order, 10*i+j) }()
		}
	}
}

func recoverInBody() (r any) {
	defer func() { r = recover() }()
	for i := range seq(3) {
		if i == 1 {
			panic(i)
		}
	}
	return nil
}

func gotoOut() int {
	for i := range seq(5) {
		if i == 2 {
			goto done
		}
	}
	return -1
done:
	return 2
}

func gotoOutOfNested() int {
	n := 0
	for range seq(3) {
		for j := range seq(3) {
			n++
			if j == 1 {
				goto done
			}
		}
	}
done:
	return n
}

func gotoLoop() int {
	rounds := 0
again:
	for i := range seq(3) {
		if i == 1 && rounds < 2 {
			rounds++
			goto again
		}
	}
	return rounds
}

func main() {
	cprover.Assert(deferInBody() == 3, "the loop ran")
	cprover.Assert(len(order) == 4, "all deferred calls ran")
	cprover.Assert(order[0] == 2 && order[1] == 1 && order[2] == 0,
		"in the reverse order")
	cprover.Assert(order[3] == 100, "after those of the loop")

	order = nil
	deferInNested()
	cprover.Assert(len(order) == 4 && order[0] == 11 && order[3] == 0,
		"nested bodies defer to the outermost function")

	cprover.Assert(recoverInBody() == 1, "a panic of the body is recovered")
	cprover.Assert(gotoOut() == 2, "goto leaves the body")
	cprover.Assert(gotoOutOfNested() == 2, "and nested bodies")
	cprover.Assert(gotoLoop() == 2, "goto the loop's label starts it anew")
}
