/// \file
/// TypeScript language frontend for CBMC

#ifndef CPROVER_TYPESCRIPT_TYPESCRIPT_LANGUAGE_H
#define CPROVER_TYPESCRIPT_TYPESCRIPT_LANGUAGE_H

#include <json/json_parser.h>
#include <langapi/language.h>

class typescript_languaget : public languaget
{
public:
  bool parse(
    std::istream &instream,
    const std::string &path,
    message_handlert &message_handler) override;

  bool typecheck(
    symbol_table_baset &symbol_table,
    const std::string &module,
    message_handlert &message_handler) override;

  bool generate_support_functions(
    symbol_table_baset &symbol_table,
    message_handlert &message_handler) override;

  void set_language_options(
    const optionst &options,
    message_handlert &message_handler) override;

  bool from_expr(const exprt &expr, std::string &code, const namespacet &ns)
    override;

  bool from_type(const typet &type, std::string &code, const namespacet &ns)
    override;

  bool to_expr(
    const std::string &code,
    const std::string &module,
    exprt &expr,
    const namespacet &ns,
    message_handlert &message_handler) override;

  void
  show_parse(std::ostream &out, message_handlert &message_handler) override;

  std::string id() const override
  {
    return "typescript";
  }

  std::string description() const override
  {
    return "TypeScript";
  }

  std::set<std::string> extensions() const override
  {
    return {"ts"};
  }

  std::unique_ptr<languaget> new_language() override
  {
    return std::make_unique<typescript_languaget>();
  }

protected:
  std::string filename;
  jsont ast_json;
};

std::unique_ptr<languaget> new_typescript_language();

#endif // CPROVER_TYPESCRIPT_TYPESCRIPT_LANGUAGE_H
