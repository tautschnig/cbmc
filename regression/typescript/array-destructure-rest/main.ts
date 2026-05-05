const [first, second, ...rest] = [10, 20, 30, 40, 50];
console.assert(first === 10);
console.assert(second === 20);
console.assert(rest.length === 3);
console.assert(rest[0] === 30);
console.assert(rest[1] === 40);
console.assert(rest[2] === 50);
