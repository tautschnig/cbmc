// Security: __CPROVER_assert_safe_property_key catches "__proto__".
function main(): void {
    const key: string = "__proto__";
    __CPROVER_assert_safe_property_key(key);
}
main();
