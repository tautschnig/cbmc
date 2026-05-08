// ES2024 §24.1.3.3 Map.delete / §24.1.3.1 Map.clear
const m: Map<string, number> = new Map();
m.set("a", 1);
m.set("b", 2);
m.set("c", 3);
console.assert(m.size === 3);

// delete
m.delete("b");
console.assert(m.size === 2);
console.assert(!m.has("b"));       // was the key
console.assert(m.has("a"));        // others still present
console.assert(m.has("c"));

// delete non-existent
m.delete("z");
console.assert(m.size === 2);      // unchanged

// clear
m.clear();
console.assert(m.size === 0);
console.assert(!m.has("a"));
console.assert(!m.has("c"));

// Re-set after clear
m.set("x", 99);
console.assert(m.size === 1);
console.assert(m.get("x") === 99);
