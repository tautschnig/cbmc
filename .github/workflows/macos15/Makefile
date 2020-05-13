PKGDIR ?= cbmc-latest
PKGTAR ?= $(PKGDIR).tar.gz
REPODIR ?= .

default: $(PKGTAR)

$(PKGDIR):
	brew install cmake ninja openjdk maven
	# See https://github.com/diffblue/cbmc/issues/4956 for clang issue
	cd $(REPODIR) && cmake -S. -Bbuild -DCMAKE_C_COMPILER=/usr/bin/clang -DWITH_JBMC=YES -GNinja
	cd $(REPODIR) && cmake --build build

$(PKGTAR): $(PKGDIR)
	mkdir -p ${PKGDIR}
	cp $(REPODIR)/build/bin/cbmc ${PKGDIR}
	cp $(REPODIR)/build/bin/goto-analyzer ${PKGDIR}
	cp $(REPODIR)/build/bin/goto-cc ${PKGDIR}
	cp $(REPODIR)/build/bin/goto-diff ${PKGDIR}
	cp $(REPODIR)/build/bin/goto-gcc ${PKGDIR}
	cp $(REPODIR)/build/bin/goto-harness ${PKGDIR}
	cp $(REPODIR)/build/bin/goto-instrument ${PKGDIR}
	cp $(REPODIR)/build/bin/goto-ld ${PKGDIR}
	cp $(REPODIR)/build/bin/janalyzer ${PKGDIR}
	cp $(REPODIR)/build/bin/java-unit ${PKGDIR}
	cp $(REPODIR)/build/bin/jbmc ${PKGDIR}
	cp $(REPODIR)/build/bin/jdiff ${PKGDIR}

	mkdir -p ${PKGDIR}/man/man1
	cp $(REPODIR)/doc/man/cbmc.1 ${PKGDIR}/man/man1

	tar fcz ${PKGTAR} ${PKGDIR}

clean:
	$(RM) *~
	$(RM) -r $(PKGTAR) $(PKGDIR)

.PHONY: default clean
