// Regression for: ES2024 §7.2.15 Abstract Relational Comparison on
// strings — strings compare lexicographically by UTF-16 code unit.
// Prior implementation passed string structs through
// binary_relation_exprt with ID_lt etc., which has no defined
// semantics for struct types.
function main(): void {
    console.assert("apple" < "banana");
    console.assert("banana" > "apple");
    console.assert("abc" <= "abc");
    console.assert("abc" >= "abc");

    // Shorter prefix is less
    console.assert("ab" < "abc");

    // Case-sensitive: ASCII uppercase < lowercase
    console.assert("B" < "a");
    console.assert("Z" < "a");
}
main();
