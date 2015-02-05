#include "pretty_instruction.h"

std::string pretty_instruction_baset::operator()(
    goto_programt::const_targett instruction)
{
  std::stringstream ss;
  std::string pretty_ins =
      instruction->is_goto()          ? escape(pretty_goto(instruction))
    : instruction->is_return()        ? escape(pretty_return(instruction))
    : instruction->is_assign()        ? escape(pretty_assign(instruction))
    : instruction->is_function_call() ? escape(pretty_function_call(instruction))
    : instruction->is_throw()         ? escape(pretty_throw(instruction))
    : instruction->is_catch()         ? escape(pretty_catch(instruction))
    : instruction->is_skip()          ? escape(pretty_skip(instruction))
    : instruction->is_location()      ? escape(pretty_location(instruction))
    : instruction->is_other()         ? escape(pretty_other(instruction))
    : instruction->is_decl()          ? escape(pretty_decl(instruction))
    : instruction->is_dead()          ? escape(pretty_dead(instruction))
    : instruction->is_assume()        ? escape(pretty_assume(instruction))
    : instruction->is_assert()        ? escape(pretty_assert(instruction))
    : instruction->is_atomic_begin()  ? escape(pretty_atomic_begin(instruction))
    : instruction->is_atomic_end()    ? escape(pretty_atomic_end(instruction))
    : instruction->is_start_thread()  ? escape(pretty_start_thread(instruction))
    : instruction->is_end_thread()    ? escape(pretty_end_thread(instruction))
    : instruction->is_end_function()  ? escape(pretty_end_function(instruction))
    : throw "Trying to print impossible instruction type";

  ss << escape(pre_string(instruction));
  ss << pretty_ins;
  ss << escape(post_string(instruction));

  return ss.str();
}

std::string plain_pretty_instructiont::pretty_goto(
    goto_programt::const_targett ins)
{
  std::stringstream ss;
  goto_programt::targetst::iterator it;
  goto_programt::targetst targets = ins->targets;
  if(ins->guard.is_true())
  {
    ss << "Goto ";
    for(it = targets.begin(); it != targets.end(); it++)
    {
      goto_programt::instructiont target = **it;
      ss << target.location_number;
      if(it != targets.end())
        ss << ", ";
    }
    return ss.str();
  }

  std::string t = from_expr(ns, "", ins->guard);
  while(t[t.size() - 1]  ==  '\n')
    t = t.substr(0, t.size() - 1);

  ss << t << "\n" << "true? GOTO ";
  for(it = targets.begin(); it != targets.end(); it++)
  {
    goto_programt::instructiont target = **it;
    ss << target.location_number;
    if(it != targets.end())
      ss << ", ";
  }
  return ss.str();
}

std::string plain_pretty_instructiont::pretty_return(
    goto_programt::const_targett ins)
{
  return pretty_assign(ins);
}

std::string plain_pretty_instructiont::pretty_assign(
    goto_programt::const_targett ins)
{
  std::string t = from_expr(ns, "", ins->code);
  while(t[t.size() - 1] == '\n')
    t = t.substr(0, t.size() - 1);

  return t;
}

std::string plain_pretty_instructiont::pretty_function_call(
    goto_programt::const_targett ins)
{
  std::string t = from_expr(ns, "", ins->code);
  while(t[t.size() - 1] == '\n')
    t = t.substr(0, t.size() - 1);

  return t;
}

std::string plain_pretty_instructiont::pretty_throw(
    goto_programt::const_targett ins)
{
  return "THROW";
}

std::string plain_pretty_instructiont::pretty_catch(
    goto_programt::const_targett ins)
{
  return "CATCH";
}

std::string plain_pretty_instructiont::pretty_skip(
    goto_programt::const_targett ins)
{
  return "SKIP";
}

std::string plain_pretty_instructiont::pretty_location(
    goto_programt::const_targett ins)
{
  return "LOCATION";
}

std::string plain_pretty_instructiont::pretty_other(
    goto_programt::const_targett ins)
{
  return pretty_assign(ins);
}

std::string plain_pretty_instructiont::pretty_decl(
    goto_programt::const_targett ins)
{
  std::string t = from_expr(ns, "", ins->code);
  while(t[t.size() - 1] == '\n')
    t = t.substr(0, t.size() - 1);

  return t;
}

std::string plain_pretty_instructiont::pretty_dead(
    goto_programt::const_targett ins)
{
  return "DEAD";
}

std::string plain_pretty_instructiont::pretty_assume(
    goto_programt::const_targett ins)
{
  std::string t = from_expr(ns, "", ins->guard);
  while(t[t.size() - 1] == '\n')
    t = t.substr(0, t.size() - 1);

  return "ASSUME \n(" + t + ")";
}

std::string plain_pretty_instructiont::pretty_assert(
    goto_programt::const_targett ins)
{
  std::string t = from_expr(ns, "", ins->guard);
  while(t[t.size() - 1] == '\n')
    t = t.substr(0, t.size() - 1);

  return "ASSERT \n(" + t + ")";
}

std::string plain_pretty_instructiont::pretty_atomic_begin(
    goto_programt::const_targett ins)
{
  return "ATOMIC BEGIN";
}

std::string plain_pretty_instructiont::pretty_atomic_end(
    goto_programt::const_targett ins)
{
  return "ATOMIC END";
}

std::string plain_pretty_instructiont::pretty_start_thread(
    goto_programt::const_targett ins)
{
  return "START THREAD";
}

std::string plain_pretty_instructiont::pretty_end_thread(
    goto_programt::const_targett ins)
{
  return "END THREAD";
}

std::string plain_pretty_instructiont::pretty_end_function(
    goto_programt::const_targett ins)
{
  return "END OF FUNCTION";
}

inline std::string plain_pretty_instructiont::pre_string(
    goto_programt::const_targett ins)
{
  return "";
}

inline std::string plain_pretty_instructiont::post_string(
    goto_programt::const_targett ins)
{
  return "";
}

inline std::string plain_pretty_instructiont::escape(std::string str)
{
  return str;
}

std::string cfg_pretty_instructiont::pre_string(
    goto_programt::const_targett ins)
{
  std::stringstream ss;
  ss << ins->location_number << "\n";
  return ss.str();
}

std::string cfg_pretty_instructiont::post_string(
    goto_programt::const_targett ins)
{
  return "";
}

std::string cfg_pretty_instructiont::escape(std::string str)
{
  for(unsigned i = 0; i < str.size(); i++)
  {
    if(str[i] == '\n')
    {
      str[i] = 'n';
      str.insert(i, "\\");
    }
    else if
    (  str[i] == '\"'
    || str[i] == '|'
    || str[i] == '\\'
    || str[i] == '>'
    || str[i] == '<'
    || str[i] == '{'
    || str[i] == '}'
    || str[i] == '['
    || str[i] == ']'
    ){
      str.insert(i, "\\");
      i++;
    }
  }
  return str;
}
