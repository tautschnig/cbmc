// Simplified validator.js-style email check
function isEmail(s: string): boolean {
  const atPos: number = s.indexOf("@");
  if (atPos === -1) return false;
  if (atPos === 0) return false;
  const dotPos: number = s.indexOf(".");
  if (dotPos === -1) return false;
  if (dotPos < atPos) return false;
  return true;
}
console.assert(isEmail("user@example.com") === true);
console.assert(isEmail("noat.example.com") === false);
console.assert(isEmail("@example.com") === false);
console.assert(isEmail("user@nodot") === false);
