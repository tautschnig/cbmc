// Security: input-size assertion works on strings (length field).
function main(): void {
    const s: string = "hello";
    __CPROVER_assert_input_size_bounded(s, 10);
}
main();
