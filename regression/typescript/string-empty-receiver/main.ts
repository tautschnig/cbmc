// ES2024 §22.1: string methods must work on empty-string receivers.
// Previously the dispatcher guarded on !sv.empty() which skipped all
// methods for "". Fixed by tracking sv_known separately from sv.empty().
console.assert("".length === 0);
console.assert("".concat("x") === "x");
console.assert("".concat("a", "b", "c") === "abc");
console.assert("".indexOf("x") === -1);
console.assert("".indexOf("") === 0);
console.assert("".padStart(3, "0") === "000");
console.assert("".padEnd(3, "0") === "000");
console.assert("".repeat(5) === "");
console.assert("".trim() === "");
console.assert("".trimStart() === "");
console.assert("".trimEnd() === "");
console.assert("".split(",").length === 1);
console.assert("".toUpperCase() === "");
console.assert("".toLowerCase() === "");
