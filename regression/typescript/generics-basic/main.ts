// TSH: Type Manipulation/Generics.md
function identity<T>(x: T): T { return x; }
console.assert(identity(42) === 42);
console.assert(identity(7) === 7);
