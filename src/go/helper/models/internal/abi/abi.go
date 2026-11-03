// Copyright 2023 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: only NoEscape, which hides a pointer
// from gc's escape analysis.

// Package abi models functions of the standard library's internal/abi.
package abi

import "unsafe"

// NoEscape hides the pointer p from escape analysis, preventing it
// from escaping to the heap. It compiles down to nothing.
func NoEscape(p unsafe.Pointer) unsafe.Pointer {
	return p
}
