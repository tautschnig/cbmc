// Copyright 2018 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: package internal/bytealg is its portable
// implementation (go1.27, see sourceModels in models.go) except for
// MakeNoZero, which the runtime implements: its capacity is gc's for the
// allocation (runtime.roundupsize), which is the capacity of appending n
// bytes to a nil slice (see rt.GrowCapGC), and the bytes are zero.

package bytealg

// MakeNoZero makes a slice of length n and capacity of at least n Bytes
// without zeroing the bytes (including the bytes between len and cap).
// It is the caller's responsibility to ensure uninitialized bytes
// do not leak to the end user.
func MakeNoZero(n int) []byte {
	return append([]byte(nil), make([]byte, n)...)
}
