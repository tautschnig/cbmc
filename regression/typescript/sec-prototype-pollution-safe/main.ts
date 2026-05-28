// Security: __CPROVER_assert_safe_property_key on a safe key.
function main(): void {
    const key: string = "username";
    __CPROVER_assert_safe_property_key(key);
}
main();
