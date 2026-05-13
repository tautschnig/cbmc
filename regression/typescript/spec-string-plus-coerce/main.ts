// Regression for: ES2024 §13.15.3 string + number coercion. A float
// value whose rounded-to-integer form differs from itself (e.g. 3.14)
// must be stringified with the fractional part. An earlier bug used
// `ieee_floatt::round_to_integral()` as a statement, but the method
// returns a new value rather than modifying the receiver — so `rounded
// == v` was always true and 3.14 was rendered "3".
function main(): void {
    console.assert("count: " + 42 === "count: 42");
    console.assert("value: " + true === "value: true");
    console.assert("" + 0 === "0");
    console.assert("pi: " + 3.14 === "pi: 3.14");
}
main();
