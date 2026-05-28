// Security: not-null assertion holds when a symbolic value is
// explicitly constrained to be non-nullish.
function main(): void {
    const x: number = nondet_number();
    __CPROVER_assume(x !== null && x !== undefined);
    __CPROVER_assert_not_null(x);
}
main();
