// Methods of standard-library types without a model are reported, whether
// called directly or through an interface.
package main

import (
	"fmt"
	"strings"
)

func main() {
	var b strings.Builder
	b.WriteString("x")
	var s fmt.Stringer = &b
	_ = s.String()
}
