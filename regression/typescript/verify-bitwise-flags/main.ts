const READ: number = 1;
const WRITE: number = 2;
const EXEC: number = 4;
const perms: number = READ | WRITE;
console.assert((perms & READ) !== 0);
console.assert((perms & WRITE) !== 0);
console.assert((perms & EXEC) === 0);
