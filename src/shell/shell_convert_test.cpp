/// \file
/// Shell converter: the test utility.

#include "shell_convert_class.h"

// ------------------------------------------------------------------
// The test utility (XCU test)

void shell_convertert::status_from(
  const exprt &condition,
  code_blockt &dest,
  const source_locationt &loc)
{
  assign(
    dest,
    status(),
    if_exprt{
      condition,
      from_integer(0, status_type()),
      from_integer(1, status_type())},
    loc);
}

/// ! applied to a test result: 0 and 1 are swapped, errors (2) stay.
void shell_convertert::negate_status(
  code_blockt &dest,
  const source_locationt &l)
{
  assign(
    dest,
    status(),
    if_exprt{
      equal_exprt{status(), from_integer(0, status_type())},
      from_integer(1, status_type()),
      if_exprt{
        equal_exprt{status(), from_integer(1, status_type())},
        from_integer(0, status_type()),
        status()}},
    l);
}

void shell_convertert::dynamic_operator(const source_locationt &loc)
{
  unsupported(
    loc,
    "test operand that is an expansion where its value could change how "
    "the arguments are parsed (an operator such as '!' or '-n')");
}

bool shell_convertert::is_unary_primary(const std::string &op) const
{
  static const std::set<std::string> posix{
    "-b",
    "-c",
    "-d",
    "-e",
    "-f",
    "-g",
    "-h",
    "-L",
    "-n",
    "-p",
    "-r",
    "-S",
    "-s",
    "-t",
    "-u",
    "-w",
    "-x",
    "-z"};
  return posix.count(op) != 0;
}

bool shell_convertert::is_binary_primary(const std::string &op) const
{
  static const std::set<std::string> posix{
    "=",
    "!=",
    "-eq",
    "-ne",
    "-gt",
    "-ge",
    "-lt",
    "-le",
    "-ef",
    "-nt",
    "-ot",
    "<",
    ">"};
  if(posix.count(op) != 0)
    return true;
  // "==" is an extension of bash, mksh, and zsh; dash rejects it.
  return op == "==" && program.dialect != shell_dialectt::POSIX;
}

void shell_convertert::test(
  std::vector<test_argumentt> args,
  code_blockt &dest,
  const source_locationt &loc)
{
  // POSIX specifies the results by the number of arguments; with more
  // than four, results are unspecified.
  switch(args.size())
  {
  case 0:
    set_status(dest, 1, loc);
    return;
  case 1:
    test1(args[0], dest, loc);
    return;
  case 2:
    test2(args[0], args[1], dest, loc);
    return;
  case 3:
    test3(args[0], args[1], args[2], dest, loc);
    return;
  case 4:
    test4(args, dest, loc);
    return;
  default:
    unsupported(loc, "test with more than four arguments");
  }
}

void shell_convertert::test1(
  const test_argumentt &a,
  code_blockt &dest,
  const source_locationt &loc)
{
  status_from(not_exprt{is_empty(a.value)}, dest, loc);
}

void shell_convertert::test2(
  const test_argumentt &a,
  const test_argumentt &b,
  code_blockt &dest,
  const source_locationt &loc)
{
  if(!a.text.has_value())
    dynamic_operator(loc);
  if(*a.text == "!")
  {
    status_from(is_empty(b.value), dest, loc);
    return;
  }
  if(is_unary_primary(*a.text))
  {
    unary_test(*a.text, b.value, dest, loc);
    return;
  }
  // Unspecified: shells differ.
  nondet_status(dest, 2, loc);
}

void shell_convertert::test3(
  const test_argumentt &a,
  const test_argumentt &b,
  const test_argumentt &c,
  code_blockt &dest,
  const source_locationt &loc)
{
  if(!b.text.has_value())
    dynamic_operator(loc);
  if(is_binary_primary(*b.text))
  {
    binary_test(a.value, *b.text, c.value, dest, loc);
    return;
  }
  if(!a.text.has_value())
    dynamic_operator(loc);
  if(*a.text == "!")
  {
    test2(b, c, dest, loc);
    negate_status(dest, loc);
    return;
  }
  if(*a.text == "(" && c.text.has_value() && *c.text == ")")
  {
    test1(b, dest, loc);
    return;
  }
  nondet_status(dest, 2, loc);
}

void shell_convertert::test4(
  const std::vector<test_argumentt> &args,
  code_blockt &dest,
  const source_locationt &loc)
{
  if(!args[0].text.has_value())
    dynamic_operator(loc);
  if(*args[0].text == "!")
  {
    test3(args[1], args[2], args[3], dest, loc);
    negate_status(dest, loc);
    return;
  }
  if(*args[0].text == "(" && args[3].text.has_value() && *args[3].text == ")")
  {
    test2(args[1], args[2], dest, loc);
    return;
  }
  nondet_status(dest, 2, loc);
}

void shell_convertert::unary_test(
  const std::string &op,
  const exprt &operand,
  code_blockt &dest,
  const source_locationt &loc)
{
  if(op == "-n")
    status_from(not_exprt{is_empty(operand)}, dest, loc);
  else if(op == "-z")
    status_from(is_empty(operand), dest, loc);
  else
  {
    // File tests and -t: the file system and terminals are not modelled.
    nondet_status(dest, 1, loc);
  }
}

void shell_convertert::binary_test(
  const exprt &lhs,
  const std::string &op,
  const exprt &rhs,
  code_blockt &dest,
  const source_locationt &loc)
{
  if(op == "=" || op == "==" || op == "!=")
  {
    const exprt same = equal(lhs, rhs, dest, loc);
    status_from(op == "!=" ? exprt{not_exprt{same}} : same, dest, loc);
  }
  else if(
    op.size() == 3 && op[0] == '-' && op != "-ef" && op != "-nt" && op != "-ot")
    integer_test(lhs, op, rhs, dest, loc);
  else
  {
    // -ef, -nt, -ot (files) and < > (collation order) are not modelled.
    nondet_status(dest, 1, loc);
  }
}

void shell_convertert::integer_test(
  const exprt &lhs,
  const std::string &op,
  const exprt &rhs,
  code_blockt &dest,
  const source_locationt &loc)
{
  // zsh rejects trailing white space; bash accepts space and tab, dash
  // isspace() bytes, and mksh some (assumed to be isspace() bytes).
  const trailing_blankst trailing =
    program.dialect == shell_dialectt::ZSH    ? trailing_blankst::NONE
    : program.dialect == shell_dialectt::BASH ? trailing_blankst::SPACE_TAB
                                              : trailing_blankst::ANY;
  const symbol_exprt l = temporary(integer_type(), dest, loc);
  const symbol_exprt r = temporary(integer_type(), dest, loc);
  const exprt l_valid = test_integer(lhs, trailing, l, dest, loc);
  const exprt r_valid = test_integer(rhs, trailing, r, dest, loc);
  const exprt valid = and_exprt{l_valid, r_valid};

  const irep_idt relation = op == "-eq"   ? ID_equal
                            : op == "-ne" ? ID_notequal
                            : op == "-gt" ? ID_gt
                            : op == "-ge" ? ID_ge
                            : op == "-lt" ? ID_lt
                                          : ID_le;
  code_blockt compare;
  status_from(binary_relation_exprt{l, relation, r}, compare, loc);
  code_blockt invalid;
  if(
    program.dialect == shell_dialectt::POSIX ||
    program.dialect == shell_dialectt::BASH)
  {
    set_status(invalid, 2, loc);
  }
  else
  {
    // mksh and zsh evaluate other operands as arithmetic expressions.
    nondet_status(invalid, 2, loc);
  }
  add(
    dest, code_ifthenelset{valid, std::move(compare), std::move(invalid)}, loc);
}
