// Security: __CPROVER_assert_constant_string fails on symbolic input.
function main(): void {
    const userInput: string = nondet_string();
    __CPROVER_assert_constant_string(userInput);
}
main();
