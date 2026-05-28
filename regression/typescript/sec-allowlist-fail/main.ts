// Security: allowlist assertion fails for "constructor" (method injection).
function main(): void {
    const allowed: string[] = ["alpha", "digit", "upper"];
    __CPROVER_assert_in_allowlist("constructor", allowed);
}
main();
