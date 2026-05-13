// Regression for: ES2024 §6.1.4 String type — strings are sequences
// of UTF-16 code units. Prior implementation iterated the input
// byte-by-byte, treating UTF-8 multi-byte sequences as multiple
// length units. With the UTF-8 → UTF-16 decoder added 2026-05-13,
// BMP characters that need multiple UTF-8 bytes count as one code
// unit.
function main(): void {
    // Latin-1 (2-byte UTF-8, 1 code unit)
    const s1 = "\u00e9";   // é
    console.assert(s1.length === 1);

    // BMP CJK (3-byte UTF-8, 1 code unit)
    const s2 = "\u4e2d";   // 中
    console.assert(s2.length === 1);

    // Mixed
    const s3 = "a\u00e9b";
    console.assert(s3.length === 3);
}
main();
