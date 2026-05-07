// Access control: admin-only operations
enum Role { User = 0, Admin = 1 }
class AccessGuard {
  role: Role;
  constructor(r: Role) { this.role = r; }
  canDelete(): boolean { return this.role === Role.Admin; }
  canRead(): boolean { return true; }
}
const user = new AccessGuard(Role.User);
const admin = new AccessGuard(Role.Admin);
console.assert(user.canRead() === true);
console.assert(user.canDelete() === false);
console.assert(admin.canDelete() === true);
