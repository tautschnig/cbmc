const arr: number[] = [1, 2, 3, 4, 5];
const indexSum: number = arr.reduce((acc: number, x: number, i: number): number => acc + i, 0);
console.assert(indexSum === 10); // 0+1+2+3+4
