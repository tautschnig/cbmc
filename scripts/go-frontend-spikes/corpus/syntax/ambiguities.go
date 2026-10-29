// Spike corpus: syntactically valid constructs that the native parser
// resolves heuristically. The parse trees must match go/parser (checked by
// oracle/compare_counts.py via check_native.sh, which counts TypeParams, ArrayType, SliceType,
// CompositeLit, Call, ... per file). Parse-only: this file need not
// type-check.
package p

const N, P, Q = 4, 2, 3

var x [3]int

// Go1.27 §Type_parameter_declarations: "type T[P C]" vs "type T [N]E".
type A [N]int                        // array
type B [len(x)]int                   // array: "(" after "[ IDENT"
type C [P * Q]int                    // array: "P *Q" is an expression
type D[P *Q,] struct{}               // generic: trailing comma
type E[P any] struct{ v P }          // generic
type F[K comparable, V any] map[K]V  // generic, two parameters
type G[P ~int | ~string] []P         // generic, union constraint
type H[P interface{ *int }] struct{} // generic, interface constraint
type I[P []int] struct{}             // generic, slice constraint

// Parameters: "a [4]byte" and "src []int" (name + type) vs "G[int]".
func f1(a [4]byte, src []int, g G[int], xs ...int) {}
func f2(E[int], []byte)                            {}

// Struct fields: "a [N]int" (field) vs "E[int]" (embedded generic).
type S struct {
	a [N]int
	E[int]
	*D[int]
	b    []int
	c, d int "tag"
}

func f3(ch chan int, m map[string]int, n int) {
	// Go1.27 §Composite_literals: in statement headers a composite literal
	// operand must be parenthesised.
	if x == ([3]int{}) {
	}
	for _, v := range []int{1, 2} {
		_ = v
	}
	// Function literal as call argument.
	run(func(k int) int { return k })
	// Range over int and channel (Go1.27 §For_range).
	for range n {
	}
	for v := range ch {
		_ = v
	}
	// Explicit instantiation in calls and composite literals.
	_ = Sum[int](nil)
	_ = F[int, string]{}
	// Conversions to non-name types.
	_ = (*int)(nil)
	_ = []byte("x")
	_ = (<-chan int)(nil)
	// Full slice expression.
	_ = src()[1:2:3]
	select {
	case v, ok := <-ch:
		_, _ = v, ok
	case ch <- 1:
	default:
	}
	// Label on an empty statement.
done:
	;
	goto done
}
