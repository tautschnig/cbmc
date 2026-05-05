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

  typet t = converter.convert_type("boolean");
  REQUIRE(t.id() == ID_bool);
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

  typet t = converter.convert_type("void");
  REQUIRE(t.id() == ID_empty);
}

TEST_CASE("convert_type: number[]", "[typescript]")
{
  symbol_tablet symbol_table;
  null_message_handlert mh;
  jsont empty_json;
  typescript_convertert converter{symbol_table, "test.ts", empty_json, mh};

  typet t = converter.convert_type("number[]");
  REQUIRE(t.id() == ID_struct);
  const auto &st = to_struct_type(t);
  REQUIRE(st.get_tag() == "typescript_array");
  REQUIRE(st.has_component("length"));
  REQUIRE(st.has_component("data"));
}

// --- expr2typescript tests ---

TEST_CASE("expr2typescript: integer constant", "[typescript]")
{
  ieee_floatt fv{
    ieee_float_spect::double_precision(),
    ieee_floatt::rounding_modet::ROUND_TO_EVEN};
  fv.from_integer(42);
  std::string result = expr2typescript(fv.to_expr(), namespacet{symbol_tablet{}});
  REQUIRE(result == "42");
}

TEST_CASE("expr2typescript: boolean true", "[typescript]")
{
  std::string result =
    expr2typescript(true_exprt{}, namespacet{symbol_tablet{}});
  REQUIRE(result == "true");
}

TEST_CASE("expr2typescript: boolean false", "[typescript]")
{
  std::string result =
    expr2typescript(false_exprt{}, namespacet{symbol_tablet{}});
  REQUIRE(result == "false");
}

// --- typescript_string_type tests ---

TEST_CASE("typescript_string_type structure", "[typescript]")
{
  typet st = typescript_string_type();
  REQUIRE(is_typescript_string_type(st));
  const auto &rst = to_refined_string_type(st);
  REQUIRE(rst.get_index_type().id() == ID_signedbv);
  REQUIRE(to_signedbv_type(rst.get_index_type()).get_width() == 32);
}
