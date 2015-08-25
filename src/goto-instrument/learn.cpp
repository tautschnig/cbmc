#include <cstring>
#include <iostream>
#include <fstream>
#include <iterator>
#include <stack>
#include <queue>
#include <algorithm>
#include <util/prefix.h>
#include <util/cprover_prefix.h>
#include <util/simplify_expr.h>
#include <ansi-c/c_types.h>
#include <goto-programs/cfg.h>
#include <goto-instrument/function.h>

// "--learn" helper functions
namespace {
const char LEARN_ENTER[] = "_Learn_function_enter";
const char LEARN_TRAP[] = "Learn_trap";
const char EXIT[] = "exit";
const char MAIN[] = "main";
const char NONDET_PREFIX[] = "nondet";
const char *NO_INSTR[] = { "assume", "exit", "Learn_Assert",
    "_Learn_function_enter", "_Learn_Assert", "_Learn_assert", "Learn_trap",
    "_start" };

bool is_internal_function(const std::string &function_name) {
  return has_prefix(function_name, CPROVER_PREFIX)
      || has_prefix(function_name, NONDET_PREFIX);
}

bool is_instrumentation_function(const std::string &function_name) {
  const char **end(NO_INSTR + sizeof(NO_INSTR) / sizeof(NO_INSTR[0]));
  return end != std::find(NO_INSTR, end, function_name);
}

const char BUILTIN[] = "<built-in-additions>";
const char STDLIB[] = "/std";

bool is_builtin(const locationt &loc) {
  const std::string &file(id2string(loc.get_file()));
  return file.find(BUILTIN) != std::string::npos
      || file.find(STDLIB) != std::string::npos;

}

bool should_not_be_instrumented(const std::string &fn, const locationt &loc) {
  return is_builtin(loc) || is_internal_function(fn)
      || is_instrumentation_function(fn);
}

symbol_tablet::symbolst::const_iterator find_or_add_indexed_function(
    symbol_tablet &st, const irep_idt &fn) {
  const irep_idt full_id(id2string(fn));
  const symbol_tablet::symbolst::const_iterator s_it(st.symbols.find(full_id));
  if (s_it != st.symbols.end()) {
    const typet &type(s_it->second.type);
    if (type.id() == ID_code) {
      const code_typet::parameterst &params(to_code_type(type).parameters());
      if (params.size() == 1 && params.begin()->type().id() == ID_signedbv) {
        return s_it;
      }
    }
    throw "Function `" + id2string(fn) + "` has wrong signature.";
  }
  const typet arg_type(signed_int_type());
  code_typet function_type;
  function_type.return_type() = empty_typet();
  function_type.parameters().push_back(code_typet::parametert(arg_type));
  symbolt new_symbol;
  new_symbol.name = full_id;
  new_symbol.base_name = fn;
  new_symbol.type = function_type;
  st.move(new_symbol);
  return st.symbols.find(full_id);
}

symbol_tablet::symbolst::const_iterator find_or_add_void_function(
    symbol_tablet &st, const irep_idt &fn) {
  const irep_idt full_id(id2string(fn));
  const symbol_tablet::symbolst::const_iterator s_it(st.symbols.find(full_id));
  if (s_it != st.symbols.end()) {
    const typet &type(s_it->second.type);
    if (type.id() == ID_code && to_code_type(type).parameters().empty()) {
      return s_it;
    }
    throw "Function `" + id2string(fn) + "` has wrong signature.";
  }
  code_typet function_type;
  function_type.return_type() = empty_typet();
  symbolt new_symbol;
  new_symbol.name = full_id;
  new_symbol.base_name = fn;
  new_symbol.type = function_type;
  st.move(new_symbol);
  return st.symbols.find(full_id);
}

code_function_callt to_indexed_function_call(symbol_tablet &st,
    const irep_idt &fn, int index) {
  const symbol_tablet::symbolst::const_iterator s_it(
      find_or_add_indexed_function(st, fn));
  assert(st.symbols.end() != s_it);
  code_function_callt call;
  call.function() = symbol_exprt(s_it->second.name, s_it->second.type);
  const typet arg_type(signed_int_type());
  const mp_integer width(string2integer(arg_type.get_string(ID_width)));
  const std::string binary_value(integer2string(index, 2));
  std::string bv_value((width - binary_value.length()).to_ulong(), '0');
  bv_value += binary_value;
  constant_exprt arg(bv_value, arg_type);
  call.arguments().push_back(arg);
  return call;
}

code_function_callt to_void_function_call(symbol_tablet &st,
    const irep_idt &fn) {
  const symbol_tablet::symbolst::const_iterator s_it(
      find_or_add_void_function(st, fn));
  assert(st.symbols.end() != s_it);
  code_function_callt call;
  call.function() = symbol_exprt(s_it->second.name, s_it->second.type);
  return call;
}

void instrument_function_enter(symbol_tablet &st,
    goto_functionst::function_mapt::iterator f_it, int index) {
  goto_programt &body(f_it->second.body);
  goto_programt::instructionst &instrs(body.instructions);
  goto_programt::targett instr(body.insert_before(instrs.begin()));
  instr->make_function_call(to_indexed_function_call(st, LEARN_ENTER, index));
  instr->function = f_it->first;
}

void instrument_main_exit(symbol_tablet &st,
    goto_functionst::function_mapt::iterator f_it) {
  goto_programt &body(f_it->second.body);
  goto_programt::instructionst &instrs(body.instructions);
  if (instrs.empty() || !instrs.back().is_end_function()) {
    body.add_instruction(END_FUNCTION);
  }
  for (__typeof__(instrs.begin()) it(instrs.begin()); it != instrs.end(); ++it) {
    if (it->is_return()) {
      goto_programt::instructiont call;
      call.function = f_it->first;
      call.make_function_call(to_void_function_call(st, LEARN_TRAP));
      body.insert_before_swap(it, call);
      ++it;
    }
  }
}

void instrument_learn_trap(symbol_tablet &st,
    goto_functionst::function_mapt::iterator f_it) {
  goto_programt &body(f_it->second.body);
  goto_programt::instructionst &instrs(body.instructions);
  for (__typeof__(instrs.begin()) it(instrs.begin()); it != instrs.end(); ++it) {
    if (it->is_function_call()) {
      const code_function_callt &call(to_code_function_call(it->code));
      const exprt &f(call.function());
      if (ID_symbol == f.id()) {
        if (EXIT == id2string(to_symbol_expr(f).get_identifier())) {
          goto_programt::instructiont call;
          call.function = f_it->first;
          call.make_function_call(to_void_function_call(st, LEARN_TRAP));
          body.insert_before_swap(it, call);
          ++it;
        }
      }
    }
  }
  if (MAIN == id2string(f_it->first)) {
    instrument_main_exit(st, f_it);
  }
}

std::set<std::string> read_functions(const std::string &path) {
  std::set<std::string> result;
  if (path.empty()) {
    return result;
  }
  std::ifstream ifs(path.c_str());
  std::copy(std::istream_iterator<std::string>(ifs),
      std::istream_iterator<std::string>(),
      std::inserter(result, result.end()));
  return result;
}

bool is_permitted_by_learn_functions_file(const std::set<std::string> &fs,
    const std::string &f) {
  return fs.empty() || fs.find(f) != fs.end();
}
}

/*******************************************************************
 Function: instrument_functions_for_learn

 Inputs:

 Outputs:

 Purpose:

 \*******************************************************************/

void instrument_functions_for_learn(symbol_tablet &st, goto_functionst &gf,
    const std::string &ff) {
  const std::set<std::string> fs(read_functions(ff));
  goto_functionst::function_mapt &fm(gf.function_map);
  std::vector<std::string> funcs;
  int function_index(-1);
  for (__typeof__(fm.begin()) f_it(fm.begin()); f_it != fm.end(); ++f_it) {
    const std::string &fn(id2string(f_it->first));
    if (should_not_be_instrumented(fn, f_it->second.type.location())) {
      continue;
    }
    if (MAIN != fn && is_permitted_by_learn_functions_file(fs, fn)) {
      funcs.push_back(fn);
      instrument_function_enter(st, f_it, ++function_index);
    }
    instrument_learn_trap(st, f_it);
  }
  std::ofstream ofs("func_names.data");
  for (__typeof__(funcs.begin()) it(funcs.begin()); it != funcs.end(); ++it) {
    ofs << "  " << *it << std::endl;
  }
}

// --learn-word-length helper functions
namespace {
class word_length_cfg_nodet {
  bool visited;
public:
  word_length_cfg_nodet() :
      visited(false) {
  }
  void mark_visited() {
    visited = true;
  }
  bool is_visited() {
    return visited;
  }
};

typedef cfg_baset<word_length_cfg_nodet> cfgt;
typedef std::stack<cfgt::iterator> call_stackt;
typedef std::list<call_stackt> call_stackst;
typedef std::pair<cfgt::iterator, call_stackt *> frontier_itemt;
typedef std::queue<frontier_itemt> frontiert;
frontier_itemt make_frontier_itemt(cfgt::iterator node,
    call_stackt *call_stack) {
  return std::make_pair(node, call_stack);
}

bool is_root(const cfgt::entry_mapt::value_type &entry) {
  const goto_programt::const_targett &instruction(entry.first);
  return goto_functionst::entry_point() == instruction->function
      && instruction->location_number == 0;
}

bool is_function_call(const goto_programt::const_targett &instruction) {
  if (!instruction->is_function_call()) {
    return false;
  }
  return ID_symbol == to_code_function_call(instruction->code).function().id();
}

bool is_function_call(const cfgt::entryt &node) {
  return is_function_call(node.PC);
}

const std::string &get_function_name(
    const goto_programt::const_targett &instr) {
  const exprt &f(to_code_function_call(instr->code).function());
  return id2string(to_symbol_expr(f).get_identifier());
}

const std::string &get_function_name(const cfgt::entryt &node) {
  return get_function_name(node.PC);
}

const char LEARN_ASSERT[] = "_Learn_assert";
bool is_learn_assert(const cfgt::entryt &node) {
  return is_function_call(node) && LEARN_ASSERT == get_function_name(node);
}

bool is_learn_enter(const cfgt::entryt &node) {
  return is_function_call(node) && LEARN_ENTER == get_function_name(node);
}

bool is_call_of_inlined_function(const std::set<std::string> &inlined_functions,
    const cfgt::entryt &node) {
  if (!is_function_call(node)) {
    return false;
  }
  return std::find(inlined_functions.begin(), inlined_functions.end(),
      get_function_name(node)) != inlined_functions.end();
}

bool is_empty_function_call(cfgt::entry_mapt &nodes, const cfgt::entryt &node) {
  goto_programt::const_targett next_instruction(node.PC);
  cfgt::entry_mapt::iterator next_instruction_node(
      nodes.find(++next_instruction));
  assert(next_instruction_node != nodes.end());
  return node.successors.size() == 1
      && node.successors.front() == &next_instruction_node->second;
}

bool is_in_inlined_function(const std::set<std::string> &inlined_functions,
    const cfgt::entryt &node) {
  return std::find(inlined_functions.begin(), inlined_functions.end(),
      id2string(node.PC->function)) != inlined_functions.end();
}

bool handle_inlining(cfgt::entry_mapt &nodes, frontiert &frontier,
    call_stackst &call_stacks, const std::set<std::string> &inlined_functions,
    frontier_itemt &item) {
  cfgt::entryt &node(*item.first);
  if (is_call_of_inlined_function(inlined_functions, node)
      && !is_empty_function_call(nodes, node)) {
    if (!is_in_inlined_function(inlined_functions, node)) {
      node.mark_visited();
    }
    goto_programt::const_targett next_instruction(node.PC);
    cfgt::entry_mapt::iterator next_instruction_node(
        nodes.find(++next_instruction));
    assert(next_instruction_node != nodes.end());
    item.second->push(&next_instruction_node->second);
    assert(node.successors.size() == 1);
    frontier.push(make_frontier_itemt(node.successors.front(), item.second));
    return true;
  } else if (is_in_inlined_function(inlined_functions, node)) {
    if (!node.PC->is_end_function()) {
      return false;
    }
    assert(!item.second->empty());
    frontier.push(make_frontier_itemt(item.second->top(), item.second));
    item.second->pop();
    return true;
  }
  node.mark_visited();
  return false;
}

class call_stack_splittert {
  call_stackst &call_stacks;
  call_stackt &original_call_stack;
  bool should_split;
  bool is_first;
public:
  call_stack_splittert(call_stackst &call_stacks, frontier_itemt &item) :
      call_stacks(call_stacks), original_call_stack(*item.second), should_split(
          item.first->PC->is_goto() && !item.first->PC->guard.is_true()), is_first(
          true) {
  }
  call_stackt *operator()() {
    if (!should_split) {
      return &original_call_stack;
    }
    if (is_first) {
      is_first = false;
      return &original_call_stack;
    }
    call_stacks.push_back(original_call_stack);
    return &call_stacks.back();
  }
};

void search_bfs(frontiert &frontier, call_stackst &call_stacks,
    frontier_itemt &item) {
  cfgt::entryt &node(*item.first);
  cfgt::entriest &s(node.successors);
  call_stack_splittert call_stack_slitter(call_stacks, item);
  for (__typeof__(s.begin()) it(s.begin()); it != s.end(); ++it) {
    if (!(*it)->is_visited()) {
      call_stackt *call_stack(call_stack_slitter());
      frontier.push(make_frontier_itemt(*it, call_stack));
    }
  }
}

size_t count_call_sites(const goto_functionst &gf, const std::string &fn) {
  size_t count(0);
  const __typeof__(gf.function_map) &fm(gf.function_map);
  for (__typeof__(fm.begin()) f(fm.begin()); f != fm.end(); ++f) {
    if (!f->second.body_available) {
      continue;
    }
    const goto_programt::instructionst &i(f->second.body.instructions);
    for (__typeof__(i.begin()) it(i.begin()); it != i.end(); ++it) {
      if (is_function_call(it) && get_function_name(it) == fn) {
        ++count;
      }
    }
  }
  return count;
}

void add_auto_inlining(const goto_functionst &gf,
    std::set<std::string> &inlined_functions, const size_t method_length,
    const size_t call_sites) {
  if (method_length <= 0 || call_sites <= 0) {
    return;
  }
  const __typeof__(gf.function_map) &fm(gf.function_map);
  for (__typeof__(fm.begin()) f(fm.begin()); f != fm.end(); ++f) {
    if (!f->second.body_available) {
      continue;
    }
    const goto_programt &body(f->second.body);
    if (body.instructions.size() < method_length) {
      continue;
    }
    const std::string &fn(id2string(f->first));
    if (count_call_sites(gf, fn) >= call_sites) {
      inlined_functions.insert(fn);
    }
  }
}

std::set<std::string> read_inlined_functions(const std::string &file_path) {
  std::set<std::string> result(read_functions(file_path));
  result.insert(LEARN_ENTER);
  return result;
}
}

/*******************************************************************
 Function: show_learn_word_length

 Inputs:

 Outputs:

 Purpose:

 \*******************************************************************/
void show_learn_word_length(class symbol_tablet &st, class goto_functionst &gf,
    const std::string &iff, const size_t function_length,
    const size_t call_sites) {
  std::set<std::string> inlined_functions(read_inlined_functions(iff));
  add_auto_inlining(gf, inlined_functions, function_length, call_sites);
  cfgt cfg;
  cfg(gf);
  cfgt::entry_mapt &nodes(cfg.entry_map);
  call_stackst call_stacks(1, call_stackt());
  frontiert frontier;
  frontier.push(
      make_frontier_itemt(
          &std::find_if(nodes.begin(), nodes.end(), is_root)->second,
          &call_stacks.back()));
  size_t minimum_word_length(0);
  while (!frontier.empty()) {
    frontier_itemt &item(frontier.front());
    cfgt::entryt &node(*item.first);
    if (node.is_visited()) {
      frontier.pop();
      continue;
    }
    if (is_learn_assert(node)) {
      ++minimum_word_length;
      break;
    }
    if (is_learn_enter(node)) {
      ++minimum_word_length;
    }
    if (!handle_inlining(nodes, frontier, call_stacks, inlined_functions,
        item)) {
      search_bfs(frontier, call_stacks, item);
    }
    frontier.pop();
  }
  std::cout << minimum_word_length << std::endl;
}
