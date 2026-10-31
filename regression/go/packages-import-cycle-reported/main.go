// Go1.27 §Import_declarations: "It is illegal for a package to import
// itself, directly or indirectly".
package main

import "example.com/cyc/a"

func main() { _ = a.F() }
