// Go1.27 §Appending_and_copying_slices, with the spec's examples:
//
//	var a = [...]int{0, 1, 2, 3, 4, 5, 6, 7}
//	var s = make([]int, 6)
//	var b = make([]byte, 5)
//	n1 := copy(s, a[0:])            // n1 == 6, s is []int{0, 1, 2, 3, 4, 5}
//	n2 := copy(s, s[2:])            // n2 == 4, s is []int{2, 3, 4, 5, 4, 5}
//	n3 := copy(b, "Hello, World!")  // n3 == 5, b is []byte("Hello")
//
// §Clear: "clear(s) []T sets all elements up to the length of s to the
// zero value of T". "If the map or slice is nil, clear is a no-op."
package main

import "cprover"

var a = [...]int{0, 1, 2, 3, 4, 5, 6, 7}
var s = make([]int, 6)
var b = make([]byte, 5)

func main() {
	n1 := copy(s, a[0:])
	cprover.Assert(n1 == 6 && s[0] == 0 && s[5] == 5, "n1")
	n2 := copy(s, s[2:])
	cprover.Assert(n2 == 4 && s[0] == 2 && s[3] == 5 && s[4] == 4 && s[5] == 5,
		"n2: overlapping")
	n3 := copy(b, "Hello, World!")
	cprover.Assert(n3 == 5 && string(b) == "Hello", "n3: from a string")

	t := a[2:5]
	clear(t)
	cprover.Assert(a[1] == 1 && a[2] == 0 && a[4] == 0 && a[5] == 5, "clear")
	var nilSlice []int
	clear(nilSlice)
	cprover.Assert(copy(nilSlice, t) == 0, "copy into nil copies nothing")
}
