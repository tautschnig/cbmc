enum Color { Red, Green, Blue }
const c: Color = Color.Green;
let result: number = 0;
if (c === Color.Green) { result = 1; }
console.assert(result === 1);
