/// \file
/// Unit tests for TypeScript frontend utilities

#include <testing-utils/use_catch.h>

#include <typescript/expr2typescript.h>
#include <typescript/typescript_converter.h>
#include <typescript/typescript_types.h>

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/ieee_float.h>
#include <util/namespace.h>
#include <util/std_expr.h>
#include <util/symbol_table.h>

// --- convert_type tests ---

TEST_CASE("convert_type: number", "[typescript]")
{
  symbol_tablet symbol_table;
  null_message_handlert mh;
  jsont empty_json;
  typescript_convertert converter{symbol_table, "test.ts", empty_json, mh};
  typet t = converter.convert_type("number");
  REQUIRE(t.id() == ID_floatbv);
  REQUIRE(to_floatbv_type(t).get_width() == 64);
}

TEST_CASE("convert_type: boolean", "[typescript]")
{
  symbol_tablet symbol_table;
  null_message_handlert mh;
  jsont empty_json;
  typescript_convertert converter{symbol_table, "test.ts", empty_json, mh};
  REQUIRE(converter.convert_type("boolean").id() == ID_bool);
}

TEST_CASE("convert_type: string", "[typescript]")
{
  symbol_tablet symbol_table;
  null_message_handlert mh;
  jsont empty_json;
  typescript_convertert converter{symbol_table, "test.ts", empty_json, mh};
  typet t = converter.convert_type("string");
  REQUIRE(is_typescript_string_type(t));
}

TEST_CASE("convert_type: void", "[typescript]")
{
  symbol_tablet symbol_table;
  null_message_handlert mh;
  jsont empty_json;
  typescript_convertert converter{symbol_table, "test.ts", empty_json, mh};
  REQUIRE(converter.convert_type("void").id() == ID_empty);
}

TEST_CASE("convert_type: undefined", "[typescript]")
{
  symbol_tablet symbol_table;
  null_message_handlert mh;
  jsont empty_json;
  typescript_convertert converter{symbol_table, "test.ts", empty_json, mh};
  REQUIRE(converter.convert_type("undefined").id() == ID_empty);
}

TEST_CASE("convert_type: number[]", "[typescript]")
{
  symbol_tablet symbol_table;
  null_message_handlert mh;
  jsont empty_json;
  typescript_convertert converter{symbol_table, "test.ts", empty_json, mh};
  typet t = converter.convert_type("number[]");
  REQUIRE(t.id() == ID_struct);
  REQUIRE(to_struct_type(t).get_tag() == "typescript_array");
}

TEST_CASE("convert_type: function pointer", "[typescript]")
{
  symbol_tablet symbol_table;
  null_message_handlert mh;
  jsont empty_json;
  typescript_convertert converter{symbol_table, "test.ts", empty_json, mh};
  typet t = converter.convert_type("(x: number) => number");
  REQUIRE(t.id() == ID_pointer);
}

TEST_CASE("convert_type: Promise<T> strips to T", "[typescript]")
{
  symbol_tablet symbol_table;
  null_message_handlert mh;
  jsont empty_json;
  typescript_convertert converter{symbol_table, "test.ts", empty_json, mh};
  REQUIRE(converter.convert_type("Promise<number>").id() == ID_floatbv);
}

// --- string type tests ---

TEST_CASE("typescript_string_type: struct with length and data", "[typescript]")
{
  typet st = typescript_string_type();
  REQUIRE(st.id() == ID_struct);
  REQUIRE(is_typescript_string_type(st));
  REQUIRE(to_struct_type(st).get_tag() == "typescript_string");
  REQUIRE(to_struct_type(st).components().size() == 2);
}

TEST_CASE("is_typescript_string_type: false for number", "[typescript]")
{
  REQUIRE(!is_typescript_string_type(
    floatbv_typet{ieee_float_spect::double_precision().to_type()}));
}

// --- expr2typescript tests ---

TEST_CASE("expr2typescript: integer 42", "[typescript]")
{
  symbol_tablet st;
  namespacet ns{st};
  REQUIRE(expr2typescript(from_integer(42, signedbv_typet{32}), ns) == "42");
}

TEST_CASE("expr2typescript: negative integer", "[typescript]")
{
  symbol_tablet st;
  namespacet ns{st};
  REQUIRE(expr2typescript(from_integer(-7, signedbv_typet{32}), ns) == "-7");
}

TEST_CASE("expr2typescript: true/false", "[typescript]")
{
  symbol_tablet st;
  namespacet ns{st};
  REQUIRE(expr2typescript(true_exprt{}, ns) == "true");
  REQUIRE(expr2typescript(false_exprt{}, ns) == "false");
}

// --- type2typescript tests ---

TEST_CASE("type2typescript: bool → 'boolean'", "[typescript]")
{
  symbol_tablet st;
  namespacet ns{st};
  REQUIRE(type2typescript(bool_typet{}, ns) == "boolean");
}

TEST_CASE("type2typescript: string → 'string'", "[typescript]")
{
  symbol_tablet st;
  namespacet ns{st};
  REQUIRE(type2typescript(typescript_string_type(), ns) == "string");
}

TEST_CASE("type2typescript: void → 'void'", "[typescript]")
{
  symbol_tablet st;
  namespacet ns{st};
  REQUIRE(type2typescript(empty_typet{}, ns) == "void");
}

TEST_CASE("type2typescript: signedbv → 'number'", "[typescript]")
{
  symbol_tablet st;
  namespacet ns{st};
  REQUIRE(type2typescript(signedbv_typet{32}, ns) == "number");
}
