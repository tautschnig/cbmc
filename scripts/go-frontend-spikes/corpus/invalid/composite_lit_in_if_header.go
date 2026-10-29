// Must be rejected: composite_lit_in_if_header
package p

type T struct{}
func f() { if x == T{} { } }
