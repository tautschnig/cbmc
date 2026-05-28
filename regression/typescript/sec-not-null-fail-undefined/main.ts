// Security: __CPROVER_assert_not_null catches undefined literal.
function main(): void {
    const x: number | undefined = undefined;
    __CPROVER_assert_not_null(x);
}
main();
