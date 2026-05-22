/*******************************************************************\

Module: JML Expression Tokenizer

Author: Kiro (AI agent)

\*******************************************************************/

#include "jml_tokenizer.h"

#include <cctype>
#include <unordered_map>

static const std::unordered_map<std::string, jml_token_kindt> keywords = {
  {"true", jml_token_kindt::BOOLEAN_LITERAL},
  {"false", jml_token_kindt::BOOLEAN_LITERAL},
  {"null", jml_token_kindt::NULL_LITERAL},
  {"instanceof", jml_token_kindt::INSTANCEOF},
  {"new", jml_token_kindt::NEW},
  {"this", jml_token_kindt::THIS},
  {"super", jml_token_kindt::SUPER},
  {"int", jml_token_kindt::INT},
  {"long", jml_token_kindt::LONG},
  {"boolean", jml_token_kindt::BOOLEAN},
  {"byte", jml_token_kindt::BYTE},
  {"short", jml_token_kindt::SHORT},
  {"char", jml_token_kindt::CHAR},
  {"float", jml_token_kindt::FLOAT},
  {"double", jml_token_kindt::DOUBLE},
  {"void", jml_token_kindt::VOID},
};

static const std::unordered_map<std::string, jml_token_kindt> jml_keywords = {
  {"\\result", jml_token_kindt::JML_RESULT},
  {"\\old", jml_token_kindt::JML_OLD},
  {"\\forall", jml_token_kindt::JML_FORALL},
  {"\\exists", jml_token_kindt::JML_EXISTS},
  {"\\fresh", jml_token_kindt::JML_FRESH},
  {"\\typeof", jml_token_kindt::JML_TYPEOF},
  {"\\type", jml_token_kindt::JML_TYPE},
  {"\\nonnullelements", jml_token_kindt::JML_NONNULLELEMENTS},
  {"\\nothing", jml_token_kindt::JML_NOTHING},
  {"\\everything", jml_token_kindt::JML_EVERYTHING},
};

std::vector<jml_tokent> jml_tokenize(const std::string &input)
{
  std::vector<jml_tokent> tokens;
  std::size_t pos = 0;
  const std::size_t len = input.size();

  auto peek = [&]() -> char
  { return pos < len ? input[pos] : '\0'; };
  auto advance = [&]() -> char
  { return pos < len ? input[pos++] : '\0'; };
  auto match = [&](char expected) -> bool
  {
    if(pos < len && input[pos] == expected)
    {
      ++pos;
      return true;
    }
    return false;
  };

  while(pos < len)
  {
    // Skip whitespace
    if(std::isspace(static_cast<unsigned char>(input[pos])))
    {
      ++pos;
      continue;
    }

    const std::size_t start = pos;
    const char c = advance();

    switch(c)
    {
    case '(':
      tokens.emplace_back(jml_token_kindt::LPAREN, "(", start);
      break;
    case ')':
      tokens.emplace_back(jml_token_kindt::RPAREN, ")", start);
      break;
    case '[':
      tokens.emplace_back(jml_token_kindt::LBRACKET, "[", start);
      break;
    case ']':
      tokens.emplace_back(jml_token_kindt::RBRACKET, "]", start);
      break;
    case '.':
      tokens.emplace_back(jml_token_kindt::DOT, ".", start);
      break;
    case ',':
      tokens.emplace_back(jml_token_kindt::COMMA, ",", start);
      break;
    case ';':
      tokens.emplace_back(jml_token_kindt::SEMICOLON, ";", start);
      break;
    case '?':
      tokens.emplace_back(jml_token_kindt::QUESTION, "?", start);
      break;
    case ':':
      tokens.emplace_back(jml_token_kindt::COLON, ":", start);
      break;
    case '+':
      tokens.emplace_back(jml_token_kindt::PLUS, "+", start);
      break;
    case '-':
      tokens.emplace_back(jml_token_kindt::MINUS, "-", start);
      break;
    case '*':
      tokens.emplace_back(jml_token_kindt::STAR, "*", start);
      break;
    case '/':
      tokens.emplace_back(jml_token_kindt::SLASH, "/", start);
      break;
    case '%':
      tokens.emplace_back(jml_token_kindt::PERCENT, "%", start);
      break;
    case '^':
      tokens.emplace_back(jml_token_kindt::CARET, "^", start);
      break;
    case '~':
      tokens.emplace_back(jml_token_kindt::TILDE, "~", start);
      break;
    case '&':
      if(match('&'))
        tokens.emplace_back(jml_token_kindt::AND, "&&", start);
      else
        tokens.emplace_back(jml_token_kindt::AMPERSAND, "&", start);
      break;
    case '|':
      if(match('|'))
        tokens.emplace_back(jml_token_kindt::OR, "||", start);
      else
        tokens.emplace_back(jml_token_kindt::PIPE, "|", start);
      break;
    case '!':
      if(match('='))
        tokens.emplace_back(jml_token_kindt::NE, "!=", start);
      else
        tokens.emplace_back(jml_token_kindt::BANG, "!", start);
      break;
    case '=':
      if(match('='))
      {
        if(match('>'))
          tokens.emplace_back(jml_token_kindt::IMPLIES, "==>", start);
        else
          tokens.emplace_back(jml_token_kindt::EQ, "==", start);
      }
      else
      {
        tokens.emplace_back(jml_token_kindt::ASSIGN, "=", start);
      }
      break;
    case '<':
      if(match('='))
      {
        if(match('='))
        {
          if(match('>'))
            tokens.emplace_back(jml_token_kindt::EQUIV, "<==>", start);
          else
          {
            // <=  followed by = — back up
            --pos;
            tokens.emplace_back(jml_token_kindt::LE, "<=", start);
          }
        }
        else if(match('!'))
        {
          if(pos + 1 < len && input[pos] == '=' && input[pos + 1] == '>')
          {
            pos += 2;
            tokens.emplace_back(jml_token_kindt::NOT_EQUIV, "<=!=>", start);
          }
          else
          {
            --pos;
            tokens.emplace_back(jml_token_kindt::LE, "<=", start);
          }
        }
        else
        {
          tokens.emplace_back(jml_token_kindt::LE, "<=", start);
        }
      }
      else
      {
        tokens.emplace_back(jml_token_kindt::LT, "<", start);
      }
      break;
    case '>':
      if(match('='))
        tokens.emplace_back(jml_token_kindt::GE, ">=", start);
      else
        tokens.emplace_back(jml_token_kindt::GT, ">", start);
      break;

    case '\\':
    {
      // JML keyword: \result, \old, \forall, etc.
      std::string jml_word = "\\";
      while(pos < len && std::isalpha(static_cast<unsigned char>(input[pos])))
        jml_word += input[pos++];
      auto it = jml_keywords.find(jml_word);
      if(it != jml_keywords.end())
        tokens.emplace_back(it->second, jml_word, start);
      else
        tokens.emplace_back(jml_token_kindt::ERROR, jml_word, start);
      break;
    }

    case '"':
    {
      // String literal
      std::string str = "\"";
      while(pos < len && input[pos] != '"')
      {
        if(input[pos] == '\\' && pos + 1 < len)
        {
          str += input[pos++];
          str += input[pos++];
        }
        else
        {
          str += input[pos++];
        }
      }
      if(pos < len)
        str += input[pos++]; // closing quote
      tokens.emplace_back(jml_token_kindt::STRING_LITERAL, str, start);
      break;
    }

    default:
      if(std::isdigit(static_cast<unsigned char>(c)))
      {
        // Integer literal
        std::string num(1, c);
        if(c == '0' && pos < len && (input[pos] == 'x' || input[pos] == 'X'))
        {
          num += input[pos++];
          while(pos < len &&
                std::isxdigit(static_cast<unsigned char>(input[pos])))
            num += input[pos++];
        }
        else
        {
          while(pos < len &&
                std::isdigit(static_cast<unsigned char>(input[pos])))
            num += input[pos++];
        }
        // Optional L suffix
        if(pos < len && (input[pos] == 'L' || input[pos] == 'l'))
          num += input[pos++];
        tokens.emplace_back(jml_token_kindt::INTEGER_LITERAL, num, start);
      }
      else if(
        std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$')
      {
        // Identifier or keyword
        std::string word(1, c);
        while(
          pos < len &&
          (std::isalnum(static_cast<unsigned char>(input[pos])) ||
           input[pos] == '_' || input[pos] == '$'))
        {
          word += input[pos++];
        }
        auto it = keywords.find(word);
        if(it != keywords.end())
          tokens.emplace_back(it->second, word, start);
        else
          tokens.emplace_back(jml_token_kindt::IDENTIFIER, word, start);
      }
      else
      {
        tokens.emplace_back(
          jml_token_kindt::ERROR, std::string(1, c), start);
      }
      break;
    }
  }

  tokens.emplace_back(jml_token_kindt::END_OF_INPUT, "", pos);
  return tokens;
}
