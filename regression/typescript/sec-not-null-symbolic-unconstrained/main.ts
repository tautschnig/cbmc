// Security: not-null assertion FAILS for unconstrained nondet
// (because the value could be null/undefined).
function main(): void {
    const x: number = nondet_number();
    __CPROVER_assert_not_null(x);
}
main();
