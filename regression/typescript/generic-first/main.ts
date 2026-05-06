function first<T>(a: T, b: T): T { return a; }
const x: number = first<number>(10, 20);
const y: string = first<string>("hello", "world");
console.assert(x === 10);
console.assert(y === "hello");
