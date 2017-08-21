/*******************************************************************\

Module:

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include "cpp_internal_additions.h"

#include <ostream>

#include <util/config.h>

#include <ansi-c/ansi_c_internal_additions.h>

std::string c2cpp(const std::string &s)
{
  std::string result;

  result.reserve(s.size());

  for(std::size_t i=0; i<s.size(); i++)
  {
    char ch=s[i];

    if(ch=='_' && std::string(s, i, 5)=="_Bool")
    {
      result.append("bool");
      i+=4;
      continue;
    }

    result+=ch;
  }

  return result;
}

void cpp_internal_additions(std::ostream &out)
{
  out << "# 1 \"<built-in>\"" << '\n';

  // new and delete are in the root namespace!
  out << "void operator delete(void *);" << '\n';
  out << "void *operator new(__typeof__(sizeof(int)));" << '\n';

  // auxiliaries for new/delete
  out << "extern \"C\" void *__new(__typeof__(sizeof(int)));" << '\n';
  // NOLINTNEXTLINE(whitespace/line_length)
  out << "extern \"C\" void *__new_array(__typeof__(sizeof(int)), __typeof__(sizeof(int)));" << '\n';
  // NOLINTNEXTLINE(whitespace/line_length)
  out << "extern \"C\" void *__placement_new(__typeof__(sizeof(int)), void *);" << '\n';
  // NOLINTNEXTLINE(whitespace/line_length)
  out << "extern \"C\" void *__placement_new_array(__typeof__(sizeof(int)), __typeof__(sizeof(int)), void *);" << '\n';
  out << "extern \"C\" void __delete(void *);" << '\n';
  out << "extern \"C\" void __delete_array(void *);" << '\n';
  out << "extern \"C\" bool __CPROVER_malloc_is_new_array=0;" << '\n';

  // __CPROVER namespace
  out << "namespace __CPROVER { }" << '\n';

  // types
  out << "typedef __typeof__(sizeof(int)) __CPROVER::size_t;" << '\n';
  out << "typedef __typeof__(sizeof(int)) __CPROVER_size_t;" << '\n';

  // assume/assert
  out << "extern \"C\" void assert(bool assertion);" << '\n';
  out << "extern \"C\" void __CPROVER_assume(bool assumption);" << '\n';
  out << "extern \"C\" void __CPROVER_assert("
         "bool assertion, const char *description);" << '\n';
  out << "extern \"C\" void __CPROVER::assume(bool assumption);" << '\n';
  out << "extern \"C\" void __CPROVER::assert("
         "bool assertion, const char *description);" << '\n';

  // CPROVER extensions
  out << "extern \"C\" const unsigned __CPROVER::constant_infinity_uint;\n";
  out << "extern \"C\" void __CPROVER_initialize();" << '\n';
  out << "extern \"C\" void __CPROVER::input(const char *id, ...);" << '\n';
  out << "extern \"C\" void __CPROVER::output(const char *id, ...);" << '\n';
  out << "extern \"C\" void __CPROVER::cover(bool condition);" << '\n';
  out << "extern \"C\" void __CPROVER::atomic_begin();" << '\n';
  out << "extern \"C\" void __CPROVER::atomic_end();" << '\n';

  // pointers
  out << "extern \"C\" unsigned __CPROVER_POINTER_OBJECT(const void *p);\n";
  out << "extern \"C\" signed __CPROVER_POINTER_OFFSET(const void *p);" << '\n';
  out << "extern \"C\" bool __CPROVER_DYNAMIC_OBJECT(const void *p);" << '\n';
  // NOLINTNEXTLINE(whitespace/line_length)
  out << "extern \"C\" extern unsigned char __CPROVER_memory[__CPROVER::constant_infinity_uint];" << '\n';
  out << "extern \"C\" const void *__CPROVER_dead_object=0;" << '\n';

  // malloc
  out << "extern \"C\" void *__CPROVER_malloc(__CPROVER::size_t size);" << '\n';
  out << "extern \"C\" const void *__CPROVER_deallocated=0;" << '\n';
  out << "extern \"C\" const void *__CPROVER_malloc_object=0;" << '\n';
  out << "extern \"C\" __CPROVER::size_t __CPROVER_malloc_size;" << '\n';

  // float
  out << "extern \"C\" int __CPROVER_rounding_mode;" << '\n';

  // arrays
  // NOLINTNEXTLINE(whitespace/line_length)
  out << "bool __CPROVER::array_equal(const void array1[], const void array2[]);" << '\n';
  out << "void __CPROVER::array_copy(const void dest[], const void src[]);\n";
  out << "void __CPROVER::array_set(const void dest[], ...);" << '\n';

  // GCC stuff, but also for ARM
  if(config.ansi_c.mode==configt::ansi_ct::flavourt::GCC ||
     config.ansi_c.mode==configt::ansi_ct::flavourt::APPLE ||
     config.ansi_c.mode==configt::ansi_ct::flavourt::ARM)
  {
    out << "extern \"C\" {" << '\n';
    out << c2cpp(gcc_builtin_headers_generic);
    out << c2cpp(gcc_builtin_headers_math);
    out << c2cpp(gcc_builtin_headers_mem_string);
    out << c2cpp(gcc_builtin_headers_omp);
    out << c2cpp(gcc_builtin_headers_tm);
    out << c2cpp(gcc_builtin_headers_ubsan);
    out << c2cpp(clang_builtin_headers);

    if(config.ansi_c.arch=="i386" ||
       config.ansi_c.arch=="x86_64" ||
       config.ansi_c.arch=="x32")
    {
      if(config.ansi_c.mode==configt::ansi_ct::flavourt::APPLE)
        out << "typedef double __float128;\n"; // clang doesn't do __float128

      out << c2cpp(gcc_builtin_headers_ia32);
      out << c2cpp(gcc_builtin_headers_ia32_2);
      out << c2cpp(gcc_builtin_headers_ia32_3);
      out << c2cpp(gcc_builtin_headers_ia32_4);
    }
    else if(config.ansi_c.arch=="arm64" ||
            config.ansi_c.arch=="armel" ||
            config.ansi_c.arch=="armhf" ||
            config.ansi_c.arch=="arm")
    {
      out << c2cpp(gcc_builtin_headers_arm);
    }
    else if(config.ansi_c.arch=="alpha")
    {
      out << c2cpp(gcc_builtin_headers_alpha);
    }
    else if(config.ansi_c.arch=="mips64el" ||
            config.ansi_c.arch=="mipsn32el" ||
            config.ansi_c.arch=="mipsel" ||
            config.ansi_c.arch=="mips64" ||
            config.ansi_c.arch=="mipsn32" ||
            config.ansi_c.arch=="mips")
    {
      out << c2cpp(gcc_builtin_headers_mips);
    }
    else if(config.ansi_c.arch=="powerpc" ||
            config.ansi_c.arch=="ppc64" ||
            config.ansi_c.arch=="ppc64le")
    {
      out << c2cpp(gcc_builtin_headers_power);
    }

    // On 64-bit systems, gcc has typedefs
    // __int128_t und __uint128_t -- but not on 32 bit!
    if(config.ansi_c.long_int_width>=64)
    {
      out << "typedef signed __int128 __int128_t;\n";
      out << "typedef unsigned __int128 __uint128_t;\n";
    }

    out << "}" << '\n';
  }

  // extensions for Visual C/C++
  if(config.ansi_c.os==configt::ansi_ct::ost::OS_WIN)
    out << "extern \"C\" int __noop(...);\n";

  // ARM stuff
  if(config.ansi_c.mode==configt::ansi_ct::flavourt::ARM)
    out << c2cpp(arm_builtin_headers);

  // string symbols to identify the architecture we have compiled for
  std::string architecture_strings;
  ansi_c_architecture_strings(architecture_strings);

  out << "extern \"C\" {" << '\n';
  out << architecture_strings;
  out << "}" << '\n';

  // Microsoft stuff
  if(config.ansi_c.mode==configt::ansi_ct::flavourt::VISUAL_STUDIO)
  {
    // type_info infrastructure -- the standard wants this to be in the
    // std:: namespace, but MS has it in the root namespace
    out << "class type_info;" << '\n';

    // this is the return type of __uuidof(...),
    // in the root namespace
    out << "struct _GUID;" << '\n';

    // MS ATL-related stuff
    out << "namespace ATL; " << '\n';
    out << "void ATL::AtlThrowImpl(long);" << '\n';
    out << "void __stdcall ATL::AtlThrowLastWin32();" << '\n';
  }

  out << std::flush;
}
