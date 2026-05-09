// JSON round-trip for primitives: parse(stringify(x)) === x
console.assert(JSON.parse(JSON.stringify(42)) === 42);
console.assert(JSON.parse(JSON.stringify(-7)) === -7);
console.assert(JSON.parse(JSON.stringify(true)) === true);
console.assert(JSON.parse(JSON.stringify(false)) === false);
console.assert(JSON.parse(JSON.stringify("hello")) === "hello");
console.assert(JSON.parse(JSON.stringify(0)) === 0);
