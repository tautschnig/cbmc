/*******************************************************************\

Module: COBOL bundled copybook library

Author: Kiro

\*******************************************************************/

/// \file
/// Bundled copybook text for compiler-/subsystem-supplied copybooks.

#include "cobol_copybooks.h"

#include <algorithm>
#include <map>
#include <vector>

namespace
{
/// A fixed-format source line: columns 1-7 are blank (sequence area and
/// indicator), so \p code starts in column 8, where the scanner expects it.
std::string fixed_line(const std::string &code)
{
  return std::string(7, ' ') + code + "\n";
}

/// Build a copybook of one-byte constants: a group item with one PIC X
/// elementary per name, each given a distinct one-byte VALUE. The exact byte
/// values are immaterial to verification (they are only ever compared for
/// equality against one another or against a nondeterministic field such as
/// EIBAID); distinctness is what matters, so a running counter is used.
std::string one_byte_constants(
  const std::string &group,
  const std::vector<std::string> &names)
{
  std::string s = fixed_line("01  " + group + ".");
  unsigned value = 1;
  for(const std::string &name : names)
  {
    static const char *hex = "0123456789ABCDEF";
    std::string byte;
    byte.push_back(hex[(value >> 4) & 0xF]);
    byte.push_back(hex[value & 0xF]);
    s += fixed_line("    05  " + name + " PIC X VALUE X'" + byte + "'.");
    ++value;
  }
  return s;
}

// CICS attention identifiers (DFHAID copybook). The order/membership follows
// the CICS-supplied DFHAID copybook; the byte values are modelled as distinct
// constants (see one_byte_constants).
std::string dfhaid_copybook()
{
  std::vector<std::string> names = {
    "DFHNULL",
    "DFHENTER",
    "DFHCLEAR",
    "DFHCLRP",
    "DFHPEN",
    "DFHOPID",
    "DFHMSRE",
    "DFHSTRF",
    "DFHTRIG",
    "DFHPA1",
    "DFHPA2",
    "DFHPA3"};
  for(int i = 1; i <= 24; ++i)
    names.push_back("DFHPF" + std::to_string(i));
  return one_byte_constants("DFHAID", names);
}

// CICS BMS attribute and control bytes (DFHBMSCA copybook), modelled as
// distinct one-byte constants.
std::string dfhbmsca_copybook()
{
  static const std::vector<std::string> names = {
    "DFHBMPEM", "DFHBMPNL", "DFHBMASK", "DFHBMUNP", "DFHBMUNN", "DFHBMPRO",
    "DFHBMASB", "DFHBMDAR", "DFHBMFSE", "DFHBMPRF", "DFHBMASF", "DFHBMASN",
    "DFHBMEOF", "DFHBMCUR", "DFHBMEC",  "DFHBMEN",  "DFHBMENT", "DFHBMFLG",
    "DFHBMDET", "DFHSA",    "DFHCOLOR", "DFHPS",    "DFHHLT",   "DFHBLUE",
    "DFHRED",   "DFHPINK",  "DFHGREEN", "DFHTURQ",  "DFHYELLO", "DFHNEUTR",
    "DFHBASE",  "DFHDFHI",  "DFHBLINK", "DFHREVRS", "DFHUNDLN", "DFHUNDER",
    "DFHUNNUM", "DFHPROTI", "DFHUNIMD", "DFHUNINT", "DFHALL",   "DFHERROR",
    "DFHDFCOL", "DFHDFT",   "DFHBMBRY"};
  return one_byte_constants("DFHBMSCA", names);
}

// IBM MQ object descriptor (CMQODV copybook), version-1 fields. Per the IBM MQ
// convention these copybooks are field definitions included *under* a
// program-provided 01 group (e.g. "01 MQOD. COPY CMQODV."), so they start at a
// subordinate level rather than defining their own 01.
const char *cmqodv =
  "       10  MQOD-STRUCID            PIC X(4).\n"
  "       10  MQOD-VERSION            PIC S9(9) COMP.\n"
  "       10  MQOD-OBJECTTYPE         PIC S9(9) COMP.\n"
  "       10  MQOD-OBJECTNAME         PIC X(48).\n"
  "       10  MQOD-OBJECTQMGRNAME     PIC X(48).\n"
  "       10  MQOD-DYNAMICQNAME       PIC X(48).\n"
  "       10  MQOD-ALTERNATEUSERID    PIC X(12).\n";

// IBM MQ message descriptor (CMQMDV copybook), version-1 fields. Layout per
// the MQI "MQMD - Message descriptor" structure.
const char *cmqmdv =
  "       10  MQMD-STRUCID            PIC X(4).\n"
  "       10  MQMD-VERSION            PIC S9(9) COMP.\n"
  "       10  MQMD-REPORT             PIC S9(9) COMP.\n"
  "       10  MQMD-MSGTYPE            PIC S9(9) COMP.\n"
  "       10  MQMD-EXPIRY             PIC S9(9) COMP.\n"
  "       10  MQMD-FEEDBACK           PIC S9(9) COMP.\n"
  "       10  MQMD-ENCODING           PIC S9(9) COMP.\n"
  "       10  MQMD-CODEDCHARSETID     PIC S9(9) COMP.\n"
  "       10  MQMD-FORMAT             PIC X(8).\n"
  "       10  MQMD-PRIORITY           PIC S9(9) COMP.\n"
  "       10  MQMD-PERSISTENCE        PIC S9(9) COMP.\n"
  "       10  MQMD-MSGID              PIC X(24).\n"
  "       10  MQMD-CORRELID           PIC X(24).\n"
  "       10  MQMD-BACKOUTCOUNT       PIC S9(9) COMP.\n"
  "       10  MQMD-REPLYTOQ           PIC X(48).\n"
  "       10  MQMD-REPLYTOQMGR        PIC X(48).\n"
  "       10  MQMD-USERIDENTIFIER     PIC X(12).\n"
  "       10  MQMD-ACCOUNTINGTOKEN    PIC X(32).\n"
  "       10  MQMD-APPLIDENTITYDATA   PIC X(32).\n"
  "       10  MQMD-PUTAPPLTYPE        PIC S9(9) COMP.\n"
  "       10  MQMD-PUTAPPLNAME        PIC X(28).\n"
  "       10  MQMD-PUTDATE            PIC X(8).\n"
  "       10  MQMD-PUTTIME            PIC X(8).\n"
  "       10  MQMD-APPLORIGINDATA     PIC X(4).\n";

// IBM MQ get-message options (CMQGMOV copybook), version-1 fields.
const char *cmqgmov =
  "       10  MQGMO-STRUCID           PIC X(4).\n"
  "       10  MQGMO-VERSION           PIC S9(9) COMP.\n"
  "       10  MQGMO-OPTIONS           PIC S9(9) COMP.\n"
  "       10  MQGMO-WAITINTERVAL      PIC S9(9) COMP.\n"
  "       10  MQGMO-SIGNAL1           PIC S9(9) COMP.\n"
  "       10  MQGMO-SIGNAL2           PIC S9(9) COMP.\n"
  "       10  MQGMO-RESOLVEDQNAME     PIC X(48).\n";

// IBM MQ put-message options (CMQPMOV copybook), version-1 fields.
const char *cmqpmov =
  "       10  MQPMO-STRUCID           PIC X(4).\n"
  "       10  MQPMO-VERSION           PIC S9(9) COMP.\n"
  "       10  MQPMO-OPTIONS           PIC S9(9) COMP.\n"
  "       10  MQPMO-TIMEOUT           PIC S9(9) COMP.\n"
  "       10  MQPMO-CONTEXT           PIC S9(9) COMP.\n"
  "       10  MQPMO-KNOWNDESTCOUNT    PIC S9(9) COMP.\n"
  "       10  MQPMO-UNKNOWNDESTCOUNT  PIC S9(9) COMP.\n"
  "       10  MQPMO-INVALIDDESTCOUNT  PIC S9(9) COMP.\n"
  "       10  MQPMO-RESOLVEDQNAME     PIC X(48).\n"
  "       10  MQPMO-RESOLVEDQMGRNAME  PIC X(48).\n";

// IBM MQ trigger message (CMQTML copybook). Layout per the MQI "MQTM -
// Trigger message" structure.
const char *cmqtml =
  "       10  MQTM-STRUCID            PIC X(4).\n"
  "       10  MQTM-VERSION            PIC S9(9) COMP.\n"
  "       10  MQTM-QNAME              PIC X(48).\n"
  "       10  MQTM-PROCESSNAME        PIC X(48).\n"
  "       10  MQTM-TRIGGERDATA        PIC X(64).\n"
  "       10  MQTM-APPLTYPE           PIC S9(9) COMP.\n"
  "       10  MQTM-APPLID             PIC X(256).\n"
  "       10  MQTM-ENVDATA            PIC X(128).\n"
  "       10  MQTM-USERDATA           PIC X(128).\n";

// IBM MQ named constants (CMQV copybook), the subset referenced by the
// CardDemo MQ programs. Values per the MQI constant definitions. Each is a
// standalone level-01 constant so it resolves by its unqualified name.
const char *cmqv =
  "       01  MQCC-OK                 PIC S9(9) COMP VALUE 0.\n"
  "       01  MQCC-WARNING            PIC S9(9) COMP VALUE 1.\n"
  "       01  MQCC-FAILED             PIC S9(9) COMP VALUE 2.\n"
  "       01  MQRC-NONE               PIC S9(9) COMP VALUE 0.\n"
  "       01  MQRC-NO-MSG-AVAILABLE   PIC S9(9) COMP VALUE 2033.\n"
  "       01  MQCCSI-Q-MGR            PIC S9(9) COMP VALUE 0.\n"
  "       01  MQCO-NONE               PIC S9(9) COMP VALUE 0.\n"
  "       01  MQOT-Q                  PIC S9(9) COMP VALUE 1.\n"
  "       01  MQOO-INPUT-SHARED       PIC S9(9) COMP VALUE 2.\n"
  "       01  MQOO-OUTPUT             PIC S9(9) COMP VALUE 16.\n"
  "       01  MQOO-SAVE-ALL-CONTEXT   PIC S9(9) COMP VALUE 128.\n"
  "       01  MQOO-PASS-ALL-CONTEXT   PIC S9(9) COMP VALUE 512.\n"
  "       01  MQOO-FAIL-IF-QUIESCING  PIC S9(9) COMP VALUE 8192.\n"
  "       01  MQGMO-WAIT              PIC S9(9) COMP VALUE 1.\n"
  "       01  MQGMO-SYNCPOINT         PIC S9(9) COMP VALUE 2.\n"
  "       01  MQGMO-NO-SYNCPOINT      PIC S9(9) COMP VALUE 4.\n"
  "       01  MQGMO-FAIL-IF-QUIESCING PIC S9(9) COMP VALUE 8192.\n"
  "       01  MQGMO-CONVERT           PIC S9(9) COMP VALUE 16384.\n"
  "       01  MQPMO-SYNCPOINT         PIC S9(9) COMP VALUE 2.\n"
  "       01  MQPMO-NO-SYNCPOINT      PIC S9(9) COMP VALUE 4.\n"
  "       01  MQPMO-DEFAULT-CONTEXT   PIC S9(9) COMP VALUE 32.\n"
  "       01  MQPMO-FAIL-IF-QUIESCING PIC S9(9) COMP VALUE 8192.\n"
  "       01  MQMT-REPLY              PIC S9(9) COMP VALUE 2.\n"
  "       01  MQPER-NOT-PERSISTENT    PIC S9(9) COMP VALUE 0.\n"
  "       01  MQFMT-STRING            PIC X(8) VALUE 'MQSTR'.\n"
  "       01  MQCI-NONE               PIC X(24) VALUE LOW-VALUES.\n"
  "       01  MQMI-NONE               PIC X(24) VALUE LOW-VALUES.\n";

const std::map<std::string, std::string> &library()
{
  static const std::map<std::string, std::string> m = {
    {"DFHAID", dfhaid_copybook()},
    {"DFHBMSCA", dfhbmsca_copybook()},
    {"CMQODV", cmqodv},
    {"CMQMDV", cmqmdv},
    {"CMQGMOV", cmqgmov},
    {"CMQPMOV", cmqpmov},
    {"CMQTML", cmqtml},
    {"CMQV", cmqv}};
  return m;
}
} // namespace

const std::string *cobol_builtin_copybook(const std::string &name)
{
  std::string upper = name;
  std::transform(
    upper.begin(),
    upper.end(),
    upper.begin(),
    [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  const auto it = library().find(upper);
  return it == library().end() ? nullptr : &it->second;
}
