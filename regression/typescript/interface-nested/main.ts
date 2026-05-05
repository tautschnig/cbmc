interface Address { city: string; zip: number; }
interface Person { name: string; addr: Address; }
const p: Person = { name: "Alice", addr: { city: "NYC", zip: 10001 } };
console.assert(p.addr.zip === 10001);
