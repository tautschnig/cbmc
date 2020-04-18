class Cbmc < Formula
  desc "CBMC: The C Bounded Model Checker"
  homepage "https://www.cprover.org/cbmc/"
  url "https://github.com/diffblue/cbmc/archive/cbmc-5.12.tar.gz"
  sha256 "1b9d003e1baffc714b36a38087e4ed42b47c04da5ebdb02bbce03262ea3acafd"

  depends_on "cmake" => :build

  def install
    system "cmake",
           # Try "-DCMAKE_C_COMPILER=/usr/bin/clang" if std_cmake_args fail
           # See https://github.com/diffblue/cbmc/issues/4956
           std_cmake_args,
           # The jbmc fails to build due to a missing POM for maven
           "-DWITH_JBMC=OFF",
           "-S.", "-Bbuild"
    system "cmake", "--build", "build"
    # cbmc 5.12 does not come with an install target
    bin.install "build/bin/cbmc"
    bin.install "build/bin/goto-analyzer"
    bin.install "build/bin/goto-cc"
    bin.install "build/bin/goto-diff"
    bin.install "build/bin/goto-gcc"
    bin.install "build/bin/goto-harness"
    bin.install "build/bin/goto-instrument"
  end

  test do
    # Find a pointer out of bounds error
    (testpath/"main.c").write <<~EOS
      #include <stdlib.h>
      int main() {
        char *ptr = malloc(10);
        char c = ptr[10];
      }
    EOS
    assert_match "VERIFICATION FAILED",
                 shell_output("#{bin}/cbmc --pointer-check main.c", 10)
  end
end
