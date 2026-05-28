// Security: __CPROVER_assert_safe_property_key on an assume-constrained
// symbolic key. The assume narrows to "username" so the assertion holds.
function main(): void {
    const key: string = nondet_string();
    __CPROVER_assume(key === "username");
    __CPROVER_assert_safe_property_key(key);
}
main();
