enum Status { Active = 10, Inactive = 20, Pending = 30 }
const s: Status = Status.Inactive;
console.assert(s === 20);
