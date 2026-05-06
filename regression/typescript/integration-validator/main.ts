// Input validator with error accumulation
class Validator {
  errors: number;
  fieldsChecked: number;
  
  constructor() { this.errors = 0; this.fieldsChecked = 0; }
  
  checkRange(value: number, min: number, max: number): boolean {
    this.fieldsChecked = this.fieldsChecked + 1;
    if (value < min || value > max) {
      this.errors = this.errors + 1;
      return false;
    }
    return true;
  }
  
  checkPositive(value: number): boolean {
    this.fieldsChecked = this.fieldsChecked + 1;
    if (value <= 0) {
      this.errors = this.errors + 1;
      return false;
    }
    return true;
  }
  
  isValid(): boolean { return this.errors === 0; }
}

const v = new Validator();
console.assert(v.isValid() === true);
v.checkRange(5, 0, 10);
console.assert(v.isValid() === true);
v.checkRange(50, 0, 10);  // fails
console.assert(v.isValid() === false);
console.assert(v.errors === 1);
console.assert(v.fieldsChecked === 2);
