// Methods of standard-library types without a model are reported, whether
// called directly or through an interface, and calling them fails a
// property; functions that are not called (direct) do not affect
// verification.
package main

import (
	"fmt"
	"strings"
)

func direct() {
	var b strings.Builder
	b.WriteString("x")
}

func main() {
	var s fmt.Stringer = &strings.Builder{}
	_ = s.String()
}
