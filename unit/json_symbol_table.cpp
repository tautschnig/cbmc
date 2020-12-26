/// Author: Daniel Poetzl

/// \file json symbol table read/write consistency

#include <goto-programs/goto_model.h>
#include <goto-programs/show_symbol_table.h>

#include <json-symtab-language/json_symbol_table.h>
#include <json/json_parser.h>

#include <util/cmdline.h>
#include <util/json_stream.h>
#include <util/message.h>
#include <util/symbol_table.h>
#include <util/ui_message.h>

#include <testing-utils/get_goto_model_from_c.h>
#include <testing-utils/message.h>
#include <testing-utils/use_catch.h>

#include <iosfwd>

TEST_CASE("json symbol table read/write consistency")
{
  // Get symbol table associated with goto program
  const std::string program = "int main() { return 0; }\n";
  const auto goto_model = get_goto_model_from_c(program);

  const symbol_tablet &symbol_table1 = goto_model.symbol_table;

  // Convert symbol table to json string

  std::ostringstream out;

  {
    json_ui_message_handlert ui_message_handler(
      &null_message_handler,
      "",
      false,
      timestampert::clockt::NONE,
      out);

    show_symbol_table(symbol_table1, ui_message_handler);
  }

  // Convert json string to symbol table

  symbol_tablet symbol_table2;

  {
    std::istringstream in(out.str());
    jsont json;

    bool r = parse_json(in, "", null_message_handler, json);
    REQUIRE(!r);

    REQUIRE(json.is_array());

    const json_arrayt &json_array = to_json_array(json);
    const jsont &json_symbol_table = *(json_array.begin() + 1);

    symbol_table_from_json(json_symbol_table, symbol_table2);
  }

  // Finally check if symbol tables are consistent

  REQUIRE(symbol_table1 == symbol_table2);
}
