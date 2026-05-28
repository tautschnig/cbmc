// Security: __CPROVER_assert_safe_property_key catches "constructor".
function main(): void {
    const key: string = "constructor";
    __CPROVER_assert_safe_property_key(key);
}
main();
