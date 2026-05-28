// Security: __CPROVER_assert_constant_string on a literal.
function main(): void {
    __CPROVER_assert_constant_string("eval-this-safely");
}
main();
