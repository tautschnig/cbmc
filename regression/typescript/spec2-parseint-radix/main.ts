// Regression for: ES2024 §21.1.2.5 parseInt, §21.1.2.6 parseFloat,
// §21.1.1.1 Number. Prior implementation had a placeholder `return
// from_integer(0, ...)` that returned 0 for every input. This
// commit actually parses.
function main(): void {
    // parseInt basic
    console.assert(parseInt("42") === 42);
    console.assert(parseInt("-5") === -5);
    console.assert(parseInt("3.14") === 3);         // stops at dot
    console.assert(parseInt("  42  ") === 42);      // trims whitespace

    // parseInt with radix
    console.assert(parseInt("10", 16) === 16);
    console.assert(parseInt("ff", 16) === 255);
    console.assert(parseInt("0x10") === 16);        // auto-detect hex

    // parseFloat
    console.assert(parseFloat("3.14") === 3.14);
    console.assert(parseFloat("-2.5") === -2.5);

    // Number()
    console.assert(Number("42") === 42);
    console.assert(Number("3.14") === 3.14);
}
main();
