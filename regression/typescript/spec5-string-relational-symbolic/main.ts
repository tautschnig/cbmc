// Regression for: ES2024 §7.2.15 Abstract Relational Comparison on
// strings, exercised via constant operands. Constant-string
// compare uses the conversion-time .compare() path; symbolic
// receivers route through cprover_string_compare_to_func with
// axioms s1[x] - s2[x] = res.
//
// Note: full symbolic compare also requires the receiver's
// per-character data to be propagated — see the docs for the
// matching limitation that affects every symbolic-content test.
function main(): void {
    console.assert("apple" < "banana");
    console.assert("banana" > "apple");
    console.assert(!("banana" < "apple"));

    // Empty / prefix relations
    console.assert("" < "x");
    console.assert("ab" < "abc");
    console.assert("abc" > "ab");

    // Equal strings: neither < nor >
    console.assert(!("equal" < "equal"));
    console.assert(!("equal" > "equal"));
    console.assert("equal" <= "equal");
    console.assert("equal" >= "equal");

    // Case-sensitive (uppercase < lowercase in ASCII)
    console.assert("B" < "a");
}
main();
