function identity<T>(x: T): T { return x; }
console.assert(identity(42) === 42);
console.assert(identity(true) === true);
