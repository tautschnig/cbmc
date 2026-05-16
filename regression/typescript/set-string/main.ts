// ES2024 §24.2: Set<string> with string-typed elements.
function main(): void {
    const s = new Set<string>();
    s.add("hello");
    s.add("world");
    s.add("hello");  // duplicate
    console.assert(s.size === 2);
    console.assert(s.has("hello"));
    console.assert(s.has("world"));
    console.assert(!s.has("other"));
}
main();
