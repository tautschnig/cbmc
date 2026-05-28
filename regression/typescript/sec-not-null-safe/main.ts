// Security: __CPROVER_assert_not_null on a non-null value.
function main(): void {
    const x: number = 42;
    __CPROVER_assert_not_null(x);
}
main();
