// Go1.27 §Conversions_from_slice_to_array_or_array_pointer: "In both
// cases, if the length of the slice is less than the length of the array,
// a run-time panic occurs."
package main

import "cprover"

func toArray(s []byte) (ok bool) {
	defer func() { ok = recover() == nil }()
	_ = [4]byte(s)
	return
}

func toPointer(s []string) (ok bool) {
	defer func() { ok = recover() == nil }()
	_ = (*[1]string)(s)
	return
}

func main() {
	s := make([]byte, 2, 4)
	cprover.Assert(!toArray(s), "panics: len([4]byte) > len(s)")
	cprover.Assert(toArray(s[:4]), "but not for a long enough slice")
	var t []string
	cprover.Assert(!toPointer(t), "panics: len([1]string) > len(t)")
	_ = [3]byte(s)
}
