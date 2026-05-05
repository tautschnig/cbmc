function isPalindrome(s: string): boolean {
  if (s.length <= 1) return true;
  return s.charAt(0) === s.charAt(s.length - 1);
}
console.assert(isPalindrome("a") === true);
console.assert(isPalindrome("aba") === true);
