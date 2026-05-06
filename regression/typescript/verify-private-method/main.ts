class Validator {
  private minLen: number;
  constructor(min: number) { this.minLen = min; }
  isValid(s: string): boolean { return s.length >= this.minLen; }
}
const v = new Validator(3);
console.assert(v.isValid("hello") === true);
console.assert(v.isValid("hi") === false);
