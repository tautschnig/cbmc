// The deferred calls of the body of a range over a function belong to the
// enclosing function, and goto may leave the body: neither is supported yet.
package main

func seq(yield func(int) bool) { yield(1) }

func main() {
	for v := range seq {
		defer println(v)
	}
	for range seq {
		goto out
	}
out:
}
