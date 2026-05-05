enum Perm { Read = 4, Write = 2, Execute = 1 }
const rwx: number = Perm.Read | Perm.Write | Perm.Execute;
console.assert(rwx === 7);
console.assert((rwx & Perm.Read) === Perm.Read);
console.assert((rwx & Perm.Write) === Perm.Write);
