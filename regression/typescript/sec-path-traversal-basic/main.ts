// Security: __CPROVER_assert_no_path_traversal primitive.
// Asserts that a string does not contain ".." substring.
function main(): void {
    const safe: string = "tmp123";
    __CPROVER_assert_no_path_traversal(safe);
}
main();
