const m = new Map<string, number>();
m.set("apple", 100);
m.set("banana", 50);
console.assert(m.has("apple") === true);
console.assert(m.has("cherry") === false);
console.assert(m.size === 2);
