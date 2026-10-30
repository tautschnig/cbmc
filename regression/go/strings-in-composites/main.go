// Strings in structs, arrays and slices, compared by value (Go1.27
// §Comparison_operators: "Two struct values are equal if their
// corresponding non-blank field values are equal"; "Two string values
// are compared lexically byte-wise.").
package main

import "cprover"

type Person struct {
	name string
	age  int
}

func main() {
	p := Person{"Ada" + "", 36}
	q := Person{name: "Ada", age: 36}
	cprover.Assert(p == q, "equal structs with equal strings")
	q.name = "Bob"
	cprover.Assert(p != q, "different strings")

	names := [2]string{"x", "y" + "z"}
	cprover.Assert(names == [2]string{"x", "yz"}, "arrays of strings")
	list := []string{"a", "b"}
	list = append(list, names[1])
	cprover.Assert(len(list) == 3 && list[2] == "yz", "slices of strings")
}
