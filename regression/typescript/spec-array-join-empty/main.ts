// Regression for: ES2024 §23.1.3.15 Array.prototype.join on an empty
// array returns the empty string (not the separator). Prior
// implementation returned the empty constant only when the internal
// buffer was non-empty; for an empty input it fell through to a
// symbolic fallback and the equality check failed.
function main(): void {
    console.assert([].join(",") === "");
    console.assert([].join() === "");
    console.assert([42].join(",") === "42");
    console.assert([1, 2, 3].join("-") === "1-2-3");
    console.assert([1, 2, 3].join("") === "123");
}
main();
