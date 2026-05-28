// Security: __CPROVER_assert_no_path_traversal must catch ".." in path.
function main(): void {
    const malicious: string = "../../etc/passwd";
    __CPROVER_assert_no_path_traversal(malicious);
}
main();
