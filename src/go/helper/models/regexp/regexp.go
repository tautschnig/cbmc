// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: specialBytes is a constant table.

// Package regexp overrides the initialisation of specialBytes of the
// standard library's regexp (go1.27), whose source is the model otherwise:
// the table that its init function computes from the metacharacters
// `\.+*?()|[]{}^$`, so that the package's initialisation needs no
// unwinding.
package regexp

var specialBytes = [16]byte{0, 0, 0, 0, 4, 0, 0, 0, 4, 4, 4, 164, 160, 160,
	36, 8}

func init() {}
