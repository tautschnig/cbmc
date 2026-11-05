/// \file
/// Shell converter: [[ expression ]] (bash, mksh, zsh).
///
/// Unlike the test utility, its words are not subject to field splitting
/// or pathname expansion, && and || short-circuit, the right operand of
/// == and != is a pattern, and the operands of -eq and the other integer
/// comparisons are arithmetic expressions. The file system is not
/// modelled, nor is the locale's collation, which < and > use.

#include "shell_convert_class.h"

#include <cctype>

void shell_convertert::convert_double_bracket(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  PRECONDITION(command.test.size() == 1);
  if(!command.assignments.empty())
    unsupported(loc, "variable assignment before [[ ... ]]");
  const exprt result = double_bracket(command.test.front(), dest);
  status_from(result, dest, loc);
}

exprt shell_convertert::double_bracket_integer(
  const shell_wordt &word,
  code_blockt &dest,
  const source_locationt &loc)
{
  using kindt = shell_word_partt::kindt;
  // Literal text is an arithmetic expression.
  if(const auto text = static_text(word))
  {
    const auto parsed = parse_shell_arithmetic(*text, arith_options);
    if(!parsed.expression.has_value())
      unsupported(loc, "[[ ... ]] integer operand that is not valid");
    return arith(*parsed.expression, dest, loc);
  }
  // A parameter expansion, quoted or not, whose value is an integer
  // constant (the value of a variable that is an expression would be
  // evaluated in turn).
  const shell_wordt *single = &word;
  if(
    word.size() == 1 && word.front().kind == kindt::DOUBLE_QUOTED &&
    word.front().parts.size() == 1)
  {
    single = &word.front().parts;
  }
  if(single->size() != 1 || single->front().kind != kindt::PARAMETER)
    unsupported(loc, "[[ ... ]] integer operand that is an expansion");
  const exprt value =
    expand_word(word, shell_word_contextt::CASE_SUBJECT, dest, loc);
  return arith_read(display(word), value, dest, loc);
}

exprt shell_convertert::double_bracket(
  const shell_testt &test,
  code_blockt &dest)
{
  using kindt = shell_testt::kindt;
  const source_locationt &loc = test.location;
  const symbol_exprt result = temporary(bool_typet{}, dest, loc);
  const auto operand = [&](const shell_wordt &word)
  { return expand_word(word, shell_word_contextt::CASE_SUBJECT, dest, loc); };
  // A result that is not modelled: the operands are expanded for their
  // side effects.
  const auto unknown = [&]()
  {
    for(const shell_wordt *word : {&test.word, &test.right})
    {
      if(!word->empty())
        expand_word(*word, shell_word_contextt::IGNORED, dest, loc);
    }
    assign(dest, result, nondet(bool_typet{}, loc), loc);
  };
  switch(test.kind)
  {
  case kindt::WORD:
    assign(dest, result, not_exprt{is_empty(operand(test.word))}, loc);
    return result;
  case kindt::UNARY:
  {
    static const std::set<std::string> file_tests{
      "-a", "-b", "-c", "-d", "-e", "-f", "-g", "-h", "-k", "-L", "-N",
      "-O", "-G", "-p", "-r", "-S", "-s", "-t", "-u", "-w", "-x"};
    if(test.op == "!")
    {
      assign(
        dest, result, not_exprt{double_bracket(test.operands[0], dest)}, loc);
    }
    else if(test.op == "-n")
      assign(dest, result, not_exprt{is_empty(operand(test.word))}, loc);
    else if(test.op == "-z")
      assign(dest, result, is_empty(operand(test.word)), loc);
    else if(file_tests.count(test.op) != 0)
      unknown();
    else if(test.op == "-v" && program.dialect != shell_dialectt::MKSH)
    {
      const auto name = static_text(test.word);
      if(!name.has_value())
        unsupported(loc, "[[ -v ]] of a name that is an expansion");
      assign(dest, result, is_set(variable(*name, loc)), loc);
    }
    else
      unsupported(loc, "[[ ... ]] operator " + test.op);
    return result;
  }
  case kindt::BINARY:
    if(test.op == "&&" || test.op == "||")
    {
      // The right operand is only evaluated where it decides.
      assign(dest, result, double_bracket(test.operands[0], dest), loc);
      code_blockt right;
      assign(right, result, double_bracket(test.operands[1], right), loc);
      add(
        dest,
        code_ifthenelset{
          test.op == "&&" ? exprt{result} : exprt{not_exprt{result}},
          std::move(right)},
        loc);
      return result;
    }
    if(test.op == "==" || test.op == "=" || test.op == "!=")
    {
      const exprt subject = operand(test.word);
      const exprt matches = pattern_matches(test.right, subject, dest, loc);
      assign(
        dest,
        result,
        test.op == "!=" ? exprt{not_exprt{matches}} : matches,
        loc);
      return result;
    }
    {
      static const std::map<std::string, irep_idt> comparisons{
        {"-eq", ID_equal},
        {"-ne", ID_notequal},
        {"-lt", ID_lt},
        {"-le", ID_le},
        {"-gt", ID_gt},
        {"-ge", ID_ge}};
      const auto comparison = comparisons.find(test.op);
      if(comparison != comparisons.end())
      {
        const exprt left = double_bracket_integer(test.word, dest, loc);
        const exprt right = double_bracket_integer(test.right, dest, loc);
        assign(
          dest,
          result,
          binary_relation_exprt{left, comparison->second, right},
          loc);
        return result;
      }
    }
    if(
      test.op == "<" || test.op == ">" || test.op == "-nt" ||
      test.op == "-ot" || test.op == "-ef")
    {
      unknown();
      return result;
    }
    unsupported(loc, "[[ ... ]] operator " + test.op);
  }
  UNREACHABLE;
}
