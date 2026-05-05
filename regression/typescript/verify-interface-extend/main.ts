interface HasName { name: string; }
interface HasAge { age: number; }
interface Person { name: string; age: number; }
const p: Person = { name: "Alice", age: 30 };
console.assert(p.age === 30);
