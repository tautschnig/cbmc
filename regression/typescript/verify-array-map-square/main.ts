const arr: number[] = [1, 2, 3, 4];
const squares: number[] = arr.map((x: number): number => x * x);
console.assert(squares[0] === 1);
console.assert(squares[1] === 4);
console.assert(squares[2] === 9);
console.assert(squares[3] === 16);
