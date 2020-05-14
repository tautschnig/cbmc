VERSION?=5.12

PKGDIR?=cbmc
BLDDIR?=build
PACKAGE_NAME?=cbmc_${VERSION}_amd64.deb

################################################################
# Force SCRIPT_DIR to be explicitly set on command line
# SCRIPT_DIR?=.

default: ${PACKAGE_NAME}

# Install bison, flex, and ninja
dependencies:
	sudo apt-get install bison flex ninja-build

# Build CBMC
${BLDDIR}/bin/cbmc: dependencies
	cmake -S. -B${BLDDIR} -GNinja
	cmake --build ${BLDDIR}

# Install CBMC
${PKGDIR}: ${BLDDIR}/bin/cbmc
	mkdir -p ${PKGDIR}/usr/bin
	cp ${BLDDIR}/bin/cbmc ${PKGDIR}/usr/bin
	cp ${BLDDIR}/bin/goto-analyzer ${PKGDIR}/usr/bin
	cp ${BLDDIR}/bin/goto-cc ${PKGDIR}/usr/bin
	cp ${BLDDIR}/bin/goto-diff ${PKGDIR}/usr/bin
	cp ${BLDDIR}/bin/goto-gcc ${PKGDIR}/usr/bin
	cp ${BLDDIR}/bin/goto-harness ${PKGDIR}/usr/bin
	cp ${BLDDIR}/bin/goto-instrument ${PKGDIR}/usr/bin
	cp ${BLDDIR}/bin/goto-ld ${PKGDIR}/usr/bin
	cp ${BLDDIR}/bin/janalyzer ${PKGDIR}/usr/bin
	cp ${BLDDIR}/bin/java-unit ${PKGDIR}/usr/bin
	cp ${BLDDIR}/bin/jbmc ${PKGDIR}/usr/bin
	cp ${BLDDIR}/bin/jdiff ${PKGDIR}/usr/bin

	mkdir -p ${PKGDIR}/usr/share/man/man1
	cp doc/man/cbmc.1 ${PKGDIR}/usr/share/man/man1

# Package CBMC
${PACKAGE_NAME}: ${PKGDIR}
	@if [ -z "${SCRIPT_DIR}" ]; then \
	  echo "ERROR: SCRIPT_DIR not set"; \
	  exit 1; \
	fi
	mkdir -p ${PKGDIR}/DEBIAN
	set -- `du -ks ${PKGDIR}`
	SIZE=$1
	cat ${SCRIPT_DIR}/control | sed "s/VERSION/${VERSION}/" | sed "s/SIZE/${SIZE}/" > ${PKGDIR}/DEBIAN/control

	dpkg-deb --build ${PKGDIR} ${PACKAGE_NAME}

# Clean up everything but the ${BLDDIR}
clean:
	$(RM) -r ${PKGDIR}
	$(RM) ${PACKAGE_NAME}
