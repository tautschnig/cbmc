/*******************************************************************\

Module:

Author: Daniel Kroening, kroening@cs.cmu.edu

\*******************************************************************/

#include "mode.h"

#include <cctype>
#include <fstream>
#include <list>
#include <memory>
#include <set>

#ifdef _WIN32
#include <cstring>
#endif

#include <util/invariant.h>
#include <util/namespace.h>
#include <util/symbol.h>
#include <util/unicode.h>

#include "language.h"

struct language_entryt
{
  language_factoryt factory;
  std::set<std::string> extensions;
  std::set<std::string> interpreters;
  irep_idt mode;
};

typedef std::list<language_entryt> languagest;
languagest languages;

/// Register a language
/// Note: registering a language is required for using the functions
///   in language_util.h
/// \param factory: a language factory, e.g. `new_ansi_c_language`
void register_language(language_factoryt factory)
{
  languages.push_back(language_entryt());
  std::unique_ptr<languaget> l(factory());
  languages.back().factory=factory;
  languages.back().extensions=l->extensions();
  languages.back().interpreters = l->interpreters();
  languages.back().mode=l->id();
}

/// Get the language corresponding to the given mode
/// \param mode: the mode, e.g. `ID_C`
/// \return the language or `nullptr` if the language has not been registered
std::unique_ptr<languaget> get_language_from_mode(const irep_idt &mode)
{
  for(const auto &language : languages)
    if(mode == language.mode)
      return language.factory();

  return nullptr;
}

/// Get the mode of the given identifier's symbol
/// \param ns: a namespace
/// \param identifier: an identifier
/// \return the mode, e.g. `ID_C`, if the identifier is in the given
///   symbol table, or `ID_unknown` otherwise
const irep_idt &
get_mode_from_identifier(const namespacet &ns, const irep_idt &identifier)
{
  if(identifier.empty())
    return ID_unknown;
  const symbolt *symbol;
  if(ns.lookup(identifier, symbol))
    return ID_unknown;
  return symbol->mode;
}

/// Get the language corresponding to the mode of the given identifier's symbol
/// \param ns: a namespace
/// \param identifier: an identifier
/// \return the corresponding language if the mode is not `ID_unknown`, or
///   the default language otherwise;
/// Note: It is assumed as an invariant that languages of symbols in the symbol
///   table have been registered.
std::unique_ptr<languaget>
get_language_from_identifier(const namespacet &ns, const irep_idt &identifier)
{
  const irep_idt &mode = get_mode_from_identifier(ns, identifier);
  if(mode == ID_unknown)
    return get_default_language();

  std::unique_ptr<languaget> language = get_language_from_mode(mode);
  INVARIANT(
    language,
    "symbol '" + id2string(identifier) + "' has unknown mode '" +
      id2string(mode) + "'");
  return language;
}

/// Get the language registered for the extension of \p filename.
static std::unique_ptr<languaget>
get_language_from_extension(const std::string &filename)
{
  std::size_t ext_pos=filename.rfind('.');

  if(ext_pos==std::string::npos)
    return nullptr;

  std::string extension=
    std::string(filename, ext_pos+1, std::string::npos);

  if(extension.empty())
    return nullptr;

  for(languagest::const_iterator
      l_it=languages.begin();
      l_it!=languages.end();
      l_it++)
  {
    #ifdef _WIN32
    for(std::set<std::string>::const_iterator
        e_it=l_it->extensions.begin();
        e_it!=l_it->extensions.end();
        e_it++)
      if(_stricmp(extension.c_str(), e_it->c_str())==0)
        return l_it->factory();
    #else
    if(l_it->extensions.find(extension)!=l_it->extensions.end())
      return l_it->factory();
    #endif
  }

  return nullptr;
}

/// Decode the first line of \p head, after an optional byte order mark,
/// keeping only ASCII characters: a `#!` line consists of ASCII characters
/// in practice, but may be stored as UTF-16 or UTF-32.
static std::string first_ascii_line(const std::string &head)
{
  auto starts_with = [&head](const char *bom, std::size_t size) {
    return head.compare(0, size, std::string{bom, size}) == 0;
  };

  // Code unit width and byte order; UTF-32 must be checked before UTF-16,
  // as the UTF-32LE byte order mark starts with the UTF-16LE one.
  std::size_t width = 1;
  bool little_endian = true;
  std::size_t pos = 0;
  if(starts_with("\xEF\xBB\xBF", 3))
    pos = 3;
  else if(starts_with("\xFF\xFE\x00\x00", 4))
    width = 4, pos = 4;
  else if(starts_with("\x00\x00\xFE\xFF", 4))
    width = 4, little_endian = false, pos = 4;
  else if(starts_with("\xFF\xFE", 2))
    width = 2, pos = 2;
  else if(starts_with("\xFE\xFF", 2))
    width = 2, little_endian = false, pos = 2;

  std::string line;
  for(; pos + width <= head.size(); pos += width)
  {
    unsigned long code = 0;
    for(std::size_t i = 0; i < width; ++i)
    {
      const std::size_t byte = little_endian ? width - 1 - i : i;
      code = (code << 8) | static_cast<unsigned char>(head[pos + byte]);
    }
    if(code == '\n' || code == 0 || code > 0x7f)
      break;
    line += static_cast<char>(code);
  }
  if(!line.empty() && line.back() == '\r')
    line.pop_back();
  return line;
}

std::string shebang_interpreter(const std::string &head)
{
  const std::string line = first_ascii_line(head);
  if(line.compare(0, 2, "#!") != 0)
    return {};

  std::size_t pos = 2;
  auto next_word = [&line, &pos]()
  {
    while(pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
      ++pos;
    const std::size_t start = pos;
    while(pos < line.size() && line[pos] != ' ' && line[pos] != '\t')
      ++pos;
    return line.substr(start, pos - start);
  };
  auto base_name = [](const std::string &path)
  {
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
  };

  std::string word = base_name(next_word());
  if(word == "env")
  {
    // Skip options (as in "env -S") and variable assignments.
    do
      word = next_word();
    while(!word.empty() &&
          (word[0] == '-' || word.find('=') != std::string::npos));
    word = base_name(word);
  }
  return word;
}

/// Get the language for the interpreter named on the `#!` line of the file
/// \p filename, also trying the interpreter name without a version suffix
/// (`python3.12` as `python3` and `python`, `ksh93` as `ksh`).
static std::unique_ptr<languaget>
get_language_from_shebang(const std::string &filename)
{
  std::ifstream in{widen_if_needed(filename), std::ios::binary};
  if(!in)
    return nullptr;
  // Long enough for a #! line with a long interpreter path, stored as
  // UTF-32.
  std::string head(1024, '\0');
  in.read(&head[0], static_cast<std::streamsize>(head.size()));
  head.resize(static_cast<std::size_t>(in.gcount()));

  std::string interpreter = shebang_interpreter(head);
  while(!interpreter.empty())
  {
    for(const auto &language : languages)
    {
      if(language.interpreters.count(interpreter) != 0)
        return language.factory();
    }
    // Drop a trailing version component: "python3.12" -> "python3" ->
    // "python".
    const std::size_t dot = interpreter.rfind('.');
    if(
      dot != std::string::npos &&
      interpreter.find_first_not_of("0123456789", dot + 1) == std::string::npos)
    {
      interpreter.erase(dot);
    }
    else if(isdigit(static_cast<unsigned char>(interpreter.back())))
    {
      interpreter.erase(interpreter.find_last_not_of("0123456789") + 1);
    }
    else
      break;
  }
  return nullptr;
}

/// Get the language corresponding to the registered file name extensions,
/// or, if the extension is not registered, to the interpreter named on the
/// file's `#!` line (see \ref languaget::interpreters).
/// \param filename: a filename
/// \return the corresponding language or `nullptr` if neither the
///   extension nor the `#!` line can be resolved to any registered language
std::unique_ptr<languaget>
get_language_from_filename(const std::string &filename)
{
  if(auto language = get_language_from_extension(filename))
    return language;
  return get_language_from_shebang(filename);
}

/// Returns the default language
/// \return the first registered language
std::unique_ptr<languaget> get_default_language()
{
  PRECONDITION(!languages.empty());
  return languages.front().factory();
}
