// Package regexp: classes of Unicode categories and scripts (\p{...}) need
// the tables of package unicode, which are not modelled; parsing them
// fails a property where it is reached.
package main

import (
	"cprover"
	"regexp"
)

func main() {
	ascii := regexp.MustCompile(`[a-z]+`)
	cprover.Assert(ascii.MatchString("abc"), "classes without tables work")
	greek := regexp.MustCompile(`\p{Greek}`)
	_ = greek
}
