interface Nameable { name: string; }
function greet<T extends Nameable>(x: T): string { return x.name; }
const u = { name: "Alice", age: 30 };
console.assert(greet(u) === "Alice");
