/*******************************************************************\

Module:

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include "ui_message.h"

#include <fstream>
#include <iostream>

#include "cmdline.h"
#include "cout_message.h"
#include "json.h"
#include "json_irep.h"
#include "json_stream.h"
#include "make_unique.h"
#include "structured_data.h"
#include "xml.h"
#include "xml_irep.h"

ui_message_handlert::~ui_message_handlert()
{
}

ui_message_handlert::ui_message_handlert(
  message_handlert *_message_handler,
  uit __ui,
  bool always_flush,
  timestampert::clockt clock_type)
  : message_handler(_message_handler),
    _ui(__ui),
    always_flush(always_flush),
    time(timestampert::make(clock_type))
{
}

console_ui_message_handlert::console_ui_message_handlert(
  bool always_flush,
  timestampert::clockt clock_type)
  : ui_message_handlert(
    &console_message_handler,
    uit::PLAIN,
    always_flush,
    clock_type),
  console_message_handler(always_flush)
{
}

json_ui_message_handlert::json_ui_message_handlert(
  message_handlert *_message_handler,
  const std::string &program,
  bool always_flush,
  timestampert::clockt clock_type,
  std::ostream &os)
  : ui_message_handlert(_message_handler,
                        uit::JSON_UI,
                        always_flush,
                        clock_type),
  out(os),
  json_stream(out)
{
  json_stream.push_back().make_object()["program"] = json_stringt(program);
}

xml_ui_message_handlert::xml_ui_message_handlert(
  message_handlert *_message_handler,
  const std::string &program,
  bool always_flush,
  timestampert::clockt clock_type,
  std::ostream &os)
  : ui_message_handlert(_message_handler,
                        uit::XML_UI,
                        always_flush,
                        clock_type),
  out(os)
{
  out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
      << "\n";
  out << "<cprover>"
      << "\n";

  {
    xmlt program_xml;
    program_xml.name="program";
    program_xml.data=program;

    out << program_xml;
  }
}

json_ui_message_handlert::~json_ui_message_handlert()
{
  json_stream.close();
  out << '\n';
}

xml_ui_message_handlert::~xml_ui_message_handlert()
{
  out << "</cprover>"
      << "\n";
}

const char *ui_message_handlert::level_string(unsigned level)
{
  if(level==1)
    return "ERROR";
  else if(level==2)
    return "WARNING";
  else
    return "STATUS-MESSAGE";
}

void console_ui_message_handlert::print(
  unsigned level,
  const std::string &message)
{
  if(verbosity>=level)
  {
    std::stringstream ss;
    const std::string timestamp = time->stamp();
    ss << timestamp << (timestamp.empty() ? "" : " ") << message;
    message_handler->print(level, ss.str());
    if(always_flush)
      message_handler->flush(level);
  }
}

void console_ui_message_handlert::print(
  unsigned level,
  const std::string &message,
  const source_locationt &location)
{
  message_handlert::print(level, message);

  if(verbosity>=level)
      message_handlert::print(
        level, message, location);
}

void xml_ui_message_handlert::print(
  unsigned level,
  const std::string &message)
{
  if(verbosity>=level)
  {
    source_locationt location;
    location.make_nil();
    print(level, message, location);
    if(always_flush)
      flush(level);
  }
}

void xml_ui_message_handlert::print(
  unsigned level,
  const std::string &msg1,
  const source_locationt &location)
{
  message_handlert::print(level, msg1);

  if(verbosity < level)
    return;

  std::string tmp_message(msg1);

  if(!tmp_message.empty() && *tmp_message.rbegin()=='\n')
    tmp_message.resize(tmp_message.size()-1);

  xmlt result;
  result.name="message";

  if(location.is_not_nil() &&
     !location.get_file().empty())
    result.new_element(xml(location));

  result.new_element("text").data=tmp_message;
  result.set_attribute("type", level_string(level));
  const std::string timestamp = time->stamp();
  if(!timestamp.empty())
    result.set_attribute("timestamp", timestamp);

  out << result;
  out << '\n';
}

void json_ui_message_handlert::print(
  unsigned level,
  const std::string &message)
{
  if(verbosity>=level)
  {
    source_locationt location;
    location.make_nil();
    print(level, message, location);
    if(always_flush)
      flush(level);
  }
}

void json_ui_message_handlert::print(
  unsigned level,
  const std::string &msg1,
  const source_locationt &location)
{
  message_handlert::print(level, msg1);

  if(verbosity < level)
    return;

  std::string tmp_message(msg1);

  if(!tmp_message.empty() && *tmp_message.rbegin()=='\n')
    tmp_message.resize(tmp_message.size()-1);

  json_objectt &result = json_stream.push_back().make_object();

  if(location.is_not_nil() &&
     !location.get_file().empty())
    result["sourceLocation"] = json(location);

  result["messageType"] = json_stringt(level_string(level));
  result["messageText"] = json_stringt(tmp_message);
  const std::string timestamp = time->stamp();
  if(!timestamp.empty())
    result["timestamp"] = json_stringt(timestamp);
}

void console_ui_message_handlert::flush(unsigned level)
{
  message_handler->flush(level);
}

void json_ui_message_handlert::flush(unsigned level)
{
  out << std::flush;
}

void xml_ui_message_handlert::flush(unsigned level)
{
  out << std::flush;
}

void console_ui_message_handlert::print(unsigned level, const structured_datat &data)
{
  print(level, to_pretty(data));
}

void json_ui_message_handlert::print(unsigned level, const structured_datat &data)
{
  if(verbosity>=level)
  {
    json_stream.push_back(to_json(data));
    flush(level);
  }
}

void xml_ui_message_handlert::print(unsigned level, const structured_datat &data)
{
  if(verbosity>=level)
  {
    out << to_xml(data) << '\n';
    flush(level);
  }
}
