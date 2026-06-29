# PLR §6.7: divmod(a, 0) raises ZeroDivisionError (mirrors a // 0 / a % 0).
q, r = divmod(10, 0)
