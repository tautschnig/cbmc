enum Status { Active = 1, Inactive = 0, Pending = 2 }
function isActive(s: Status): boolean { return s === Status.Active; }
console.assert(isActive(Status.Active) === true);
console.assert(isActive(Status.Inactive) === false);
console.assert(Status.Active + Status.Pending === 3);
