// Go1.27 §Type_declarations in generic functions may depend on the type
// parameters; the front-end does not substitute in them yet and reports
// them instead of dropping them.
package main

func F[T any](x T) int {
	type pair struct{ a, b T }
	p := pair{x, x}
	_ = p
	return 1
}

func main() { _ = F(1) }
