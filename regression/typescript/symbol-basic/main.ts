// ES2024 §20.4.1: Symbol() returns a unique value.
function main(): void {
    const s1 = Symbol("id");
    const s2 = Symbol("id");
    // Two Symbol() calls always produce different values
    console.assert(s1 !== s2);
    // Same symbol equals itself
    console.assert(s1 === s1);
    console.assert(s2 === s2);
}
main();
