function identity<T>(x: T): T { return x; }
const a: number = identity<number>(42);
const b: string = identity<string>("hello");
const c: boolean = identity<boolean>(true);
console.assert(a === 42);
console.assert(b === "hello");
console.assert(c === true);
