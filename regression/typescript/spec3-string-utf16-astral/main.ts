// Regression for: ES2024 §6.1.4 String — astral characters
// (code point > U+FFFF) encode as a UTF-16 surrogate pair and
// count as 2 code units in .length.
function main(): void {
    // Pile of poo (U+1F4A9) — surrogate pair → length 2
    const astral = "\u{1F4A9}";
    console.assert(astral.length === 2);

    // Mixed: "a" + astral + "b" → 1 + 2 + 1 = 4
    const mixed = "a" + astral + "b";
    console.assert(mixed.length === 4);

    // Two astral chars
    const two = "\u{1F4A9}\u{1F600}";
    console.assert(two.length === 4);
}
main();
