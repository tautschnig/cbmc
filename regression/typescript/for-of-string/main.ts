// ES2024 §22.1.3.@@iterator: for-of on a string iterates characters.
function main(): void {
    const s = "hello";
    let count = 0;
    for (const c of s) {
        count++;
    }
    console.assert(count === 5);
}
main();
