// Security: input-size assertion fails when length exceeds bound.
function main(): void {
    const arr: number[] = [1, 2, 3, 4, 5];
    __CPROVER_assert_input_size_bounded(arr, 3);
}
main();
