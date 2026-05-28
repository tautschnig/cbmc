// Security: __CPROVER_assert_safe_property_key catches "prototype".
function main(): void {
    const key: string = "prototype";
    __CPROVER_assert_safe_property_key(key);
}
main();
