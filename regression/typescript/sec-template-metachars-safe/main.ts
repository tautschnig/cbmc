// Security: no-template-metachars on a benign string.
function main(): void {
    __CPROVER_assert_no_template_metachars("Hello, world!");
}
main();
