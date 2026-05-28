// Security: input-size assertion holds for a small array.
function main(): void {
    const arr: number[] = [1, 2, 3];
    __CPROVER_assert_input_size_bounded(arr, 10);
}
main();
