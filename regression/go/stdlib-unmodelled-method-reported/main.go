// Methods of standard-library types without a model are reported, whether
// called directly or through an interface, and calling them fails a
// property; functions that are not called (direct) do not affect
// verification.
package main

import (
	"fmt"
	"go/token"
)

func direct() {
	var p token.Position
	_ = p.IsValid()
}

func main() {
	var s fmt.Stringer = token.Position{}
	_ = s.String()
}
