const arr: number[] = [10, 20, 30, 40, 50];
const first3: number[] = arr.filter((x: number, i: number): boolean => i < 3);
console.assert(first3.length === 3);
