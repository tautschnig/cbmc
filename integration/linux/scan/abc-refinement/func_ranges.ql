/**
 * @name function line ranges
 * @description Emits (absolute-path, function, startLine, endLine) for every
 *   defined function, so the differential scanner can map changed line
 *   ranges (from a git diff) to the functions a patch touched.
 * @kind problem
 * @id cpp/abc/func-ranges
 * @problem.severity warning
 */
import cpp

from Function f, int s, int e
where
  f.hasDefinition() and
  s = f.getBlock().getLocation().getStartLine() and
  e = f.getBlock().getLocation().getEndLine() and
  s > 0
select f,
  f.getFile().getAbsolutePath() + "|" + f.getName() + "|" + s.toString() + "|" +
  e.toString()
