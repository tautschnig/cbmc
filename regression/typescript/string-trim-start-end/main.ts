// ES2024 §22.1.3.30/31: trimStart / trimEnd
console.assert("  hello  ".trimStart() === "hello  ");
console.assert("  hello  ".trimEnd() === "  hello");
console.assert("\t\nhello\t\n".trimStart() === "hello\t\n");
console.assert("\t\nhello\t\n".trimEnd() === "\t\nhello");
// All whitespace
console.assert("   ".trimStart() === "");
console.assert("   ".trimEnd() === "");
// No whitespace
console.assert("hello".trimStart() === "hello");
console.assert("hello".trimEnd() === "hello");
