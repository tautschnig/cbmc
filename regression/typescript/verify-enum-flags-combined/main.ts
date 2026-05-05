enum Access { None = 0, Read = 1, Write = 2, Execute = 4 }
function canRead(perms: number): boolean { return (perms & Access.Read) !== 0; }
function canWrite(perms: number): boolean { return (perms & Access.Write) !== 0; }
const rwPerms: number = Access.Read | Access.Write;
console.assert(canRead(rwPerms) === true);
console.assert(canWrite(rwPerms) === true);
console.assert((rwPerms & Access.Execute) === 0);
