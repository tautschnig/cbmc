// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: a subset of package strings (go1.27),
// with straightforward loops instead of the optimised searches (the results
// are those the documentation specifies), and the same allocation sizes
// where they are observable (the capacities of returned slices).

// Package strings models functions of the standard library's strings.
package strings

import "cprover/rt"

const maxInt = int(^uint(0) >> 1)

// Compare returns an integer comparing two strings lexicographically. The
// result will be 0 if a == b, -1 if a < b, and +1 if a > b.
func Compare(a, b string) int {
	if a == b {
		return 0
	}
	if a < b {
		return -1
	}
	return +1
}

// HasPrefix reports whether the string s begins with prefix.
func HasPrefix(s, prefix string) bool {
	return len(s) >= len(prefix) && s[:len(prefix)] == prefix
}

// HasSuffix reports whether the string s ends with suffix.
func HasSuffix(s, suffix string) bool {
	return len(s) >= len(suffix) && s[len(s)-len(suffix):] == suffix
}

// IndexByte returns the index of the first instance of c in s, or -1 if c
// is not present in s.
func IndexByte(s string, c byte) int {
	for i := 0; i < len(s); i++ {
		if s[i] == c {
			return i
		}
	}
	return -1
}

// Index returns the index of the first instance of substr in s, or -1 if
// substr is not present in s.
func Index(s, substr string) int {
	for i := 0; i+len(substr) <= len(s); i++ {
		if s[i:i+len(substr)] == substr {
			return i
		}
	}
	return -1
}

// LastIndex returns the index of the last instance of substr in s, or -1
// if substr is not present in s.
func LastIndex(s, substr string) int {
	for i := len(s) - len(substr); i >= 0; i-- {
		if s[i:i+len(substr)] == substr {
			return i
		}
	}
	return -1
}

// Contains reports whether substr is within s.
func Contains(s, substr string) bool {
	return Index(s, substr) >= 0
}

// ContainsRune reports whether the Unicode code point r is within s.
func ContainsRune(s string, r rune) bool {
	for _, c := range s {
		if c == r {
			return true
		}
	}
	return false
}

// Count counts the number of non-overlapping instances of substr in s. If
// substr is an empty string, Count returns 1 + the number of Unicode code
// points in s.
func Count(s, substr string) int {
	if len(substr) == 0 {
		n := 0
		for range s {
			n++
		}
		return n + 1
	}
	n := 0
	for {
		i := Index(s, substr)
		if i == -1 {
			return n
		}
		n++
		s = s[i+len(substr):]
	}
}

// Repeat returns a new string consisting of count copies of the string s.
// It panics if count is negative or if the result of (len(s) * count)
// overflows.
func Repeat(s string, count int) string {
	switch count {
	case 0:
		return ""
	case 1:
		return s
	}
	if count < 0 {
		panic("strings: negative Repeat count")
	}
	if len(s) > 0 && count > maxInt/len(s) {
		panic("strings: Repeat output length overflow")
	}
	b := make([]byte, 0, len(s)*count)
	for i := 0; i < count; i++ {
		b = append(b, s...)
	}
	return string(b)
}

// TrimPrefix returns s without the provided leading prefix string. If s
// doesn't start with prefix, s is returned unchanged.
func TrimPrefix(s, prefix string) string {
	if HasPrefix(s, prefix) {
		return s[len(prefix):]
	}
	return s
}

// TrimSuffix returns s without the provided trailing suffix string. If s
// doesn't end with suffix, s is returned unchanged.
func TrimSuffix(s, suffix string) string {
	if HasSuffix(s, suffix) {
		return s[:len(s)-len(suffix)]
	}
	return s
}

// Cut slices s around the first instance of sep, returning the text before
// and after sep.
func Cut(s, sep string) (before, after string, found bool) {
	if i := Index(s, sep); i >= 0 {
		return s[:i], s[i+len(sep):], true
	}
	return s, "", false
}

// CutPrefix returns s without the provided leading prefix string and
// reports whether it found the prefix.
func CutPrefix(s, prefix string) (after string, found bool) {
	if !HasPrefix(s, prefix) {
		return s, false
	}
	return s[len(prefix):], true
}

// CutSuffix returns s without the provided ending suffix string and
// reports whether it found the suffix.
func CutSuffix(s, suffix string) (before string, found bool) {
	if !HasSuffix(s, suffix) {
		return s, false
	}
	return s[:len(s)-len(suffix)], true
}

// explode splits s into a slice of UTF-8 strings, one string per Unicode
// character up to a maximum of n (n < 0 means no limit).
func explode(s string, n int) []string {
	l := Count(s, "") - 1
	if n < 0 || n > l {
		n = l
	}
	a := make([]string, n)
	for i := 0; i < n-1; i++ {
		_, size := rt.DecodeRune(s, 0)
		a[i] = s[:size]
		s = s[size:]
	}
	if n > 0 {
		a[n-1] = s
	}
	return a
}

// genSplit splits s around each instance of sep, including sepSave bytes
// of sep in the subarrays.
func genSplit(s, sep string, sepSave, n int) []string {
	if n == 0 {
		return nil
	}
	if sep == "" {
		return explode(s, n)
	}
	if n < 0 {
		n = Count(s, sep) + 1
	}

	n = min(n, len(s)+1)
	a := make([]string, n)
	n--
	i := 0
	for i < n {
		m := Index(s, sep)
		if m < 0 {
			break
		}
		a[i] = s[:m+sepSave]
		s = s[m+len(sep):]
		i++
	}
	a[i] = s
	return a[:i+1]
}

// Split slices s into all substrings separated by sep and returns a slice
// of the substrings between those separators.
func Split(s, sep string) []string { return genSplit(s, sep, 0, -1) }

// Join concatenates the elements of its first argument to create a single
// string. The separator string sep is placed between elements in the
// resulting string.
func Join(elems []string, sep string) string {
	switch len(elems) {
	case 0:
		return ""
	case 1:
		return elems[0]
	}
	var n int
	if len(sep) > 0 {
		if len(sep) >= maxInt/(len(elems)-1) {
			panic("strings: Join output length overflow")
		}
		n += len(sep) * (len(elems) - 1)
	}
	for _, elem := range elems {
		if len(elem) > maxInt-n {
			panic("strings: Join output length overflow")
		}
		n += len(elem)
	}
	b := make([]byte, 0, n)
	b = append(b, elems[0]...)
	for _, s := range elems[1:] {
		b = append(b, sep...)
		b = append(b, s...)
	}
	return string(b)
}

// isSpace is unicode.IsSpace: '\t', '\n', '\v', '\f', '\r', ' ', U+0085
// (NEL), U+00A0 (NBSP), and the other characters with Unicode's
// White_Space property.
func isSpace(r rune) bool {
	switch r {
	case '\t', '\n', '\v', '\f', '\r', ' ', 0x85, 0xA0, 0x1680, 0x2028,
		0x2029, 0x202F, 0x205F, 0x3000:
		return true
	}
	return r >= 0x2000 && r <= 0x200A
}

// TrimSpace returns a slice of the string s, with all leading and trailing
// white space removed, as defined by Unicode.
func TrimSpace(s string) string {
	start := 0
	for start < len(s) {
		r, size := rt.DecodeRune(s, start)
		if !isSpace(r) {
			break
		}
		start += size
	}
	stop := len(s)
	for stop > start {
		// The last rune: back up over continuation bytes, at most 3.
		i := stop - 1
		for i > start && i > stop-4 && s[i]&0xC0 == 0x80 {
			i--
		}
		r, size := rt.DecodeRune(s, i)
		if i+size != stop {
			r, size = '\uFFFD', 1 // an invalid sequence: its last byte
		}
		if !isSpace(r) {
			break
		}
		stop -= size
	}
	return s[start:stop]
}

// Fields splits the string s around each instance of one or more
// consecutive white space characters, as defined by unicode.IsSpace.
func Fields(s string) []string {
	n := 0
	inField := false
	for _, r := range s {
		was := inField
		inField = !isSpace(r)
		if inField && !was {
			n++
		}
	}
	a := make([]string, n)
	na := 0
	start := -1
	for i, r := range s {
		if isSpace(r) {
			if start >= 0 {
				a[na] = s[start:i]
				na++
				start = -1
			}
		} else if start < 0 {
			start = i
		}
	}
	if start >= 0 {
		a[na] = s[start:]
	}
	return a
}
