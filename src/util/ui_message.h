/*******************************************************************\

Module:

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/


#ifndef CPROVER_UTIL_UI_MESSAGE_H
#define CPROVER_UTIL_UI_MESSAGE_H

#include <iosfwd>
#include <memory>

#include "message.h"
#include "timestamper.h"

class console_message_handlert;
class json_stream_arrayt;

class ui_message_handlert : public message_handlert
{
public:
  enum class uit { PLAIN, XML_UI, JSON_UI };

  uit get_ui() const
  {
    return _ui;
  }

  virtual ~ui_message_handlert();

protected:
  message_handlert *message_handler;
  uit _ui;
  const bool always_flush;
  std::unique_ptr<const timestampert> time;

  ui_message_handlert(
    message_handlert *,
    uit,
    bool always_flush,
    timestampert::clockt clock_type);
  ui_message_handlert(ui_message_handlert &&) = default;

  virtual void print(
    unsigned level,
    const std::string &message,
    const source_locationt &location) = 0;

  const char *level_string(unsigned level);
};

class console_ui_message_handlert : public ui_message_handlert
{
public:
  console_ui_message_handlert(
    bool always_flush,
    timestampert::clockt clock_type);
  console_ui_message_handlert(console_ui_message_handlert &&) = default;

  virtual void flush(unsigned level) override;

  void print(unsigned level, const structured_datat &data) override;

protected:
  console_message_handlert console_message_handler;

  virtual void print(
    unsigned level,
    const std::string &message) override;

  virtual void print(
    unsigned level,
    const std::string &message,
    const source_locationt &location) override;

  std::string command(unsigned c) const override
  {
    if(!message_handler)
      return std::string();

    switch(c)
    {
    case '<': // fall through
    case '>':
      return "'";
    }

    return message_handler->command(c);
  }
};

class json_ui_message_handlert : public ui_message_handlert
{
public:
  json_ui_message_handlert(
    message_handlert *,
    const std::string &program,
    bool always_flush,
    timestampert::clockt clock_type,
    std::ostream &os);
  json_ui_message_handlert(json_ui_message_handlert &&) = default;

  virtual ~json_ui_message_handlert();

  virtual void flush(unsigned level) override;

  virtual json_stream_arrayt &get_json_stream()
  {
    return json_stream;
  }
  void print(unsigned level, const structured_datat &data) override;

protected:
  std::ostream &out;
  json_stream_arrayt json_stream;

  virtual void print(
    unsigned level,
    const std::string &message) override;

  virtual void print(
    unsigned level,
    const std::string &message,
    const source_locationt &location) override;

  std::string command(unsigned c) const override
  {
    if(!message_handler)
      return std::string();

    switch(c)
    {
    case 0:  // reset
    case 1:  // bold
    case 2:  // faint
    case 3:  // italic
    case 4:  // underline
    case 31: // red
    case 32: // green
    case 33: // yellow
    case 34: // blue
    case 35: // magenta
    case 36: // cyan
    case 91: // bright_red
    case 92: // bright_green
    case 93: // bright_yellow
    case 94: // bright_blue
    case 95: // bright_magenta
    case 96: // bright_cyan
      return std::string();
    case '<': // quote_begin
    case '>': // quote_end
      return "'";
    }

    return message_handler->command(c);
  }
};

class xml_ui_message_handlert : public ui_message_handlert
{
public:
  xml_ui_message_handlert(
    message_handlert *,
    const std::string &program,
    bool always_flush,
    timestampert::clockt clock_type,
    std::ostream &os);
  xml_ui_message_handlert(xml_ui_message_handlert &&) = default;

  virtual ~xml_ui_message_handlert();

  virtual void flush(unsigned level) override;

  void print(unsigned level, const structured_datat &data) override;

protected:
  std::ostream &out;

  virtual void print(
    unsigned level,
    const std::string &message) override;

  virtual void print(
    unsigned level,
    const std::string &message,
    const source_locationt &location) override;

  std::string command(unsigned c) const override
  {
    if(!message_handler)
      return std::string();

    switch(c)
    {
    case 0:  // reset
    case 1:  // bold
    case 2:  // faint
    case 3:  // italic
    case 4:  // underline
    case 31: // red
    case 32: // green
    case 33: // yellow
    case 34: // blue
    case 35: // magenta
    case 36: // cyan
    case 91: // bright_red
    case 92: // bright_green
    case 93: // bright_yellow
    case 94: // bright_blue
    case 95: // bright_magenta
    case 96: // bright_cyan
      return std::string();
    case '<': // quote_begin
      return "<quote>";
    case '>': // quote_end
      return "</quote>";
    }

    return message_handler->command(c);
  }
};

#define OPT_FLUSH "(flush)"

#define HELP_FLUSH " --flush                      flush every line of output\n"

#endif // CPROVER_UTIL_UI_MESSAGE_H
