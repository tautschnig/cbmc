// Security: __CPROVER_assert_in_allowlist passes when value is allowed.
function main(): void {
    const allowed: string[] = ["alpha", "digit", "upper"];
    __CPROVER_assert_in_allowlist("alpha", allowed);
}
main();
