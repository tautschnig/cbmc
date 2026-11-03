// Methods of standard-library types without a model are reported, whether
// called directly or through an interface, and calling them fails a
// property; functions that are not called (direct) do not affect
// verification.
package main

import (
	"fmt"
	"regexp"
)

func direct() {
	var r regexp.Regexp
	_ = r.NumSubexp()
}

func main() {
	var s fmt.Stringer = &regexp.Regexp{}
	_ = s.String()
}
