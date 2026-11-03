// Copyright 2024 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: package internal/stringslite is the
// real one (go1.27, see sourceModels in models.go) except for Clone, which
// converts with unsafe.String there.

package stringslite

// Clone returns a fresh copy of s. It guarantees to make a copy of s into a
// new allocation.
func Clone(s string) string {
	if len(s) == 0 {
		return ""
	}
	return string([]byte(s))
}
