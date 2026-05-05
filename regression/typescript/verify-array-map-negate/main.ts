const arr: number[] = [1, -2, 3, -4, 5];
const negated: number[] = arr.map((x: number): number => -x);
console.assert(negated[0] === -1);
console.assert(negated[1] === 2);
console.assert(negated[3] === 4);
