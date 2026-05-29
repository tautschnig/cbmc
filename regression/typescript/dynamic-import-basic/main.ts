// ES2024 §13.3.10: Dynamic import — `import("./mod")`.
//
// With our sequential-async model, `await import("./mod")` evaluates
// to a struct with one field per export of "./mod". Each field has
// the spec-mandated *type*, but the *value* is nondet (we
// over-approximate; the exported symbols' actual definitions are
// not yet wired through the dynamic-import path).
//
// What this test verifies:
//   - The frontend does NOT crash on a dynamic import call.
//   - The struct has the correct shape: PI is a number, NAME is a
//     string. We can do range-bound and type checks.
//   - Assertions on the exact exported value would NOT verify (the
//     nondet over-approximation makes them indeterminate); we don't
//     assert on those.

async function main() {
    const mod = await import("./other");
    // `mod.PI` is a nondet number — any IEEE-754 double is a valid
    // value for it. We can observe its type via a no-op coercion.
    const x: number = mod.PI;
    console.assert(typeof x === "number");
    // Same for NAME — a nondet string.
    const n: string = mod.NAME;
    console.assert(typeof n === "string");
}
main();
