const m = new Map<string, number>();
m.set("key1", 100);
m.set("key2", 200);
console.assert(m.has("key1") === true);
console.assert(m.get("key1") === 100);
console.assert(m.get("key2") === 200);
console.assert(m.size === 2);
