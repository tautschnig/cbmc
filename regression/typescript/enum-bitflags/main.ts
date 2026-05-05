enum Permission { Read = 1, Write = 2, Execute = 4, All = 7 }
function hasPermission(perms: number, flag: Permission): boolean {
  return (perms & flag) !== 0;
}
const userPerms: number = Permission.Read | Permission.Write;
console.assert(hasPermission(userPerms, Permission.Read) === true);
console.assert(hasPermission(userPerms, Permission.Execute) === false);
