// Security: __CPROVER_assert_not_null catches null literal.
function main(): void {
    const x: number | null = null;
    __CPROVER_assert_not_null(x);
}
main();
