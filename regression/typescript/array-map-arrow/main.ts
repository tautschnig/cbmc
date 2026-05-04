const nums: number[] = [1, 2, 3];
const squares: number[] = nums.map((n: number): number => n * n);
console.assert(squares[0] === 1);
console.assert(squares[1] === 4);
console.assert(squares[2] === 9);
