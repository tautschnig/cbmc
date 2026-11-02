// Go1.27 §For_range: "For a function f, the iteration proceeds by calling f
// with a new, synthesized yield function as its argument." Break, continue
// (also with labels), return and closures over the iteration variables.
package main

import "cprover"

func count(n int) func(func(int) bool) {
	return func(yield func(int) bool) {
		for i := 0; i < n; i++ {
			if !yield(i) {
				return
			}
		}
	}
}

func pairs(yield func(string, int) bool) {
	_ = yield("a", 1) && yield("b", 2)
}

func find(target int) (int, bool) {
	for v := range count(5) {
		if v == target {
			return v * 10, true
		}
	}
	return 0, false
}

func main() {
	sum := 0
	for v := range count(4) {
		sum += v
	}
	cprover.Assert(sum == 6, "sum")

	n := 0
	for k, v := range pairs {
		if k == "b" {
			cprover.Assert(v == 2, "pair")
		}
		n++
	}
	cprover.Assert(n == 2, "two pairs")

	stopped := 0
	for v := range count(10) {
		if v == 3 {
			break
		}
		stopped = v
	}
	cprover.Assert(stopped == 2, "break")

	skipped := 0
	for v := range count(4) {
		if v%2 == 0 {
			continue
		}
		skipped += v
	}
	cprover.Assert(skipped == 4, "continue")

	r, ok := find(3)
	cprover.Assert(r == 30 && ok, "return")
	r, ok = find(7)
	cprover.Assert(r == 0 && !ok, "not found")

	total := 0
outer:
	for i := range count(3) {
		for j := range count(3) {
			if j > i {
				continue outer
			}
			if i == 2 {
				break outer
			}
			total += 10*i + j
		}
	}
	cprover.Assert(total == 0+10+11, "labels")

	var fs []func() int
	for v := range count(3) {
		fs = append(fs, func() int { return v })
	}
	cprover.Assert(fs[0]() == 0 && fs[2]() == 2, "per iteration")
}
