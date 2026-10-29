/*******************************************************************\

Module: Unit tests for the Go front-end configuration

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// Unit tests for configt::got, the implementation-specific widths of
/// Go's int, uint and uintptr (Go1.27 §Numeric_types).

#include <util/cmdline.h>
#include <util/config.h>

#include <testing-utils/use_catch.h>

TEST_CASE("Go integer widths follow gc", "[core][util][config][go]")
{
  configt config_backup = config;

  SECTION("Widths match the target pointer width for each GOARCH")
  {
    // set_arch requires a C flavour to have been chosen (normally done by
    // configt::set from the command line).
    config.ansi_c.mode = configt::ansi_ct::flavourt::GCC;
    config.ansi_c.os = configt::ansi_ct::ost::OS_LINUX;
    // GOARCH -> CBMC architecture name and gc's int/uintptr width.
    const std::pair<const char *, std::size_t> archs[] = {
      {"x86_64", 64},      // amd64
      {"arm64", 64},       // arm64
      {"ppc64le", 64},     // ppc64le
      {"ppc64", 64},       // ppc64
      {"riscv64", 64},     // riscv64
      {"s390x", 64},       // s390x
      {"mips64", 64},      // mips64
      {"loongarch64", 64}, // loong64
      {"i386", 32},        // 386
      {"arm", 32},         // arm
      {"mips", 32},        // mips
    };
    for(const auto &arch : archs)
    {
      config.set_arch(arch.first);
      INFO("arch " << arch.first);
      REQUIRE(config.ansi_c.pointer_width == arch.second);
      REQUIRE(config.go.int_width == arch.second);
      REQUIRE(config.go.uintptr_width == arch.second);
    }
  }

  SECTION("--32 and --64 override the host default")
  {
    const char *argv32[] = {"cbmc", "--32"};
    cmdlinet cmdline;
    REQUIRE_FALSE(cmdline.parse(2, argv32, "(32)"));
    REQUIRE_FALSE(config.set(cmdline));
    REQUIRE(config.go.int_width == 32);
    REQUIRE(config.go.uintptr_width == 32);

    const char *argv64[] = {"cbmc", "--64"};
    cmdlinet cmdline64;
    REQUIRE_FALSE(cmdline64.parse(2, argv64, "(64)"));
    REQUIRE_FALSE(config.set(cmdline64));
    REQUIRE(config.go.int_width == 64);
    REQUIRE(config.go.uintptr_width == 64);
  }

  SECTION("--arch selects the Go widths of that architecture")
  {
    const char *argv[] = {"cbmc", "--arch", "i386"};
    cmdlinet cmdline;
    REQUIRE_FALSE(cmdline.parse(3, argv, "(arch):"));
    REQUIRE_FALSE(config.set(cmdline));
    REQUIRE(config.go.int_width == 32);
  }

  config = config_backup;
}
