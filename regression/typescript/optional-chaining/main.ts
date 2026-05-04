// ES2024 sec-optional-chains: ?.
const obj = { x: 42 };
const val: number = obj?.x ?? 0;
console.assert(val === 42);
