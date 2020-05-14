PKGDIR ?= cbmc-latest
PKGTAR ?= $(PKGDIR).tar.gz
REPODIR ?= .

default: $(PKGTAR)

$(REPODIR)/build/bin/cbmc:
	brew install cmake ninja openjdk maven
	# See https://github.com/diffblue/cbmc/issues/4956 for clang issue
	cd $(REPODIR) && cmake -S. -Bbuild -DCMAKE_C_COMPILER=/usr/bin/clang -DWITH_JBMC=YES -GNinja
	cd $(REPODIR) && cmake --build build

$(PKGTAR): $(REPODIR)/build/bin/cbmc
	mkdir -p ${PKGDIR}/bin
	cp $(REPODIR)/build/bin/cbmc ${PKGDIR}/bin
	cp $(REPODIR)/build/bin/goto-analyzer ${PKGDIR}/bin
	cp $(REPODIR)/build/bin/goto-cc ${PKGDIR}/bin
	cp $(REPODIR)/build/bin/goto-diff ${PKGDIR}/bin
	cp $(REPODIR)/build/bin/goto-gcc ${PKGDIR}/bin
	cp $(REPODIR)/build/bin/goto-harness ${PKGDIR}/bin
	cp $(REPODIR)/build/bin/goto-instrument ${PKGDIR}/bin
	cp $(REPODIR)/build/bin/goto-ld ${PKGDIR}/bin
	cp $(REPODIR)/build/bin/janalyzer ${PKGDIR}/bin
	cp $(REPODIR)/build/bin/java-unit ${PKGDIR}/bin
	cp $(REPODIR)/build/bin/jbmc ${PKGDIR}/bin
	cp $(REPODIR)/build/bin/jdiff ${PKGDIR}/bin

	mkdir -p ${PKGDIR}/share/man/man1
	cp $(REPODIR)/doc/man/cbmc.1 ${PKGDIR}/share/man/man1

	tar fcz ${PKGTAR} ${PKGDIR}

clean:
	$(RM) *~
	$(RM) -r $(PKGTAR) $(PKGDIR)

.PHONY: default clean
