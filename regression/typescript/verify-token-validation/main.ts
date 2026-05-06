function isValidToken(t: string): boolean {
  if (t.length < 3) return false;
  if (t.length > 20) return false;
  return true;
}
console.assert(isValidToken("abc") === true);
console.assert(isValidToken("ab") === false);
console.assert(isValidToken("hello world") === true);
