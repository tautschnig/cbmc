// Security: allowlist with symbolic input + assume.
function main(): void {
    const allowed: string[] = ["alpha", "digit"];
    const userInput: string = nondet_string();
    __CPROVER_assume(userInput === "alpha");
    __CPROVER_assert_in_allowlist(userInput, allowed);
}
main();
