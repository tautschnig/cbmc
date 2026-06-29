# PLR §6.10: ord() expects a string of exactly one character. A constant empty or
# multi-character string raises TypeError. (Symbolic strings are not flagged.)
n = ord("ab")
