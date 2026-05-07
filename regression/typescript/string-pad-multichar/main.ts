// ES2024 §22.1.3.17: padStart with multi-char pad. The spec says
// to truncate the pad pattern to exactly fill (target - source).
console.assert("x".padStart(7, "abc") === "abcabcx");  // 6 pad chars: abc+abc
console.assert("x".padStart(4, "ab") === "abax");      // 3 pad chars: ab+a
console.assert("xy".padEnd(7, "abc") === "xyabcab");   // 5 pad chars: abc+ab
console.assert("x".padEnd(4, "ab") === "xaba");        // 3 pad chars: ab+a
