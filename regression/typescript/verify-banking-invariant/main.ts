// Banking invariant: total balance preserved under transfers
// Uses class with methods to ensure 'this' is a pointer (reference semantics)
class Bank {
  a_balance: number;
  b_balance: number;
  constructor(a: number, b: number) { this.a_balance = a; this.b_balance = b; }
  transfer(amount: number): boolean {
    if (amount <= 0) return false;
    if (this.a_balance < amount) return false;
    this.a_balance = this.a_balance - amount;
    this.b_balance = this.b_balance + amount;
    return true;
  }
  total(): number { return this.a_balance + this.b_balance; }
}
const bank = new Bank(100, 50);
const total_before: number = bank.total();
const ok: boolean = bank.transfer(30);
const total_after: number = bank.total();
console.assert(ok === true);
console.assert(bank.a_balance === 70);
console.assert(bank.b_balance === 80);
console.assert(total_before === total_after);
