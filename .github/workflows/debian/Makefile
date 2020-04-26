################################################################
# Configuration

# Package version number (cbmc version number)
VERSION=5.12

# Suffix to use to distinguish a develop/HEAD build from a release 
LATEST=-latest

# Debian package version to fetch debian/ folder from
DEBIAN_BASE=5.10-5.1

################################################################

default: build

cbmc_$(VERSION).orig-java-models-library.tar.gz:
	curl -o $@ https://github.com/diffblue/java-models-library/archive/master.tar.gz

cbmc_$(VERSION).orig.tar.gz:
ifeq (,$(LATEST))
	curl -o $@ https://github.com/diffblue/cbmc/archive/cbmc-$(VERSION).tar.gz
else
	curl -o $@ -L https://github.com/diffblue/cbmc/tarball/develop
endif

cbmc-$(VERSION): cbmc_$(VERSION).orig.tar.gz
	tar xzf $<
ifeq (,$(LATEST))
	mv cbmc-cbmc-$(VERSION) $@
else
	mv diffblue-cbmc-* $@
endif

cbmc-$(VERSION)/java-models-library: cbmc-$(VERSION) cbmc_$(VERSION).orig-java-models-library.tar.gz
	cd $<
	tar xzf ../cbmc_$(VERSION).orig-java-models-library.tar.gz
	mv java-models-library-master java-models-library

cbmc-$(VERSION)/debian: cbmc-$(VERSION)
	cd $<
	# should get existing artifacts instead
	curl -o debian.tar.xz http://deb.debian.org/debian/pool/main/c/cbmc/cbmc_$(DEBIAN_BASE).debian.tar.xz
	tar xJf debian.tar.xz
	$(RM) debian.tar.xz

cbmc-$(VERSION)/debian/changelog.orig:
	$(CP) cbmc-$(VERSION)/debian/changelog $@

cbmc-$(VERSION)/debian/changelog: cbmc-$(VERSION)/debian/changelog.orig
	cd $(dirname $@)/..
	if dpkg --compare-versions `dpkg-parsechangelog -S Version` lt $(VERSION) ; then \
		NEW_VERSION=$(VERSION)-1~auto1 ; \
	else \
	  MINOR=`dpkg-parsechangelog -S Version | cut -f2 -d- | cut -f1 -d.` ; \
		NEW_VERSION=$(VERSION)-`expr $$MINOR + 1`~auto1 ; \
	fi ; \
	cp changelog $@ ; \
	sed -i "s/#VERSION#/$$NEW_VERSION/" $@
	sed -i "s/#DATE#/`date -R`/" $@
	cat $< >> $@

build: cbmc-$(VERSION)/debian/changelog
	sudo apt-get build-dep cbmc
	cd $(dirname $<)/..
ifneq (,$(LATEST))
	sed -i "s/^Package: cbmc.*/Package: cbmc$(LATEST)/" debian/control
	sed -i "s#/usr/bin#/usr/local/cbmc$(LATEST)/bin" debian/control debian/install debian/links
endif
	dpkg-buildpackage -b -jauto -uc
	# upload new artifacts

clean:
	$(RM) cbmc-$(VERSION)
	$(RM) cbmc_$(VERSION).orig.tar.gz
	$(RM) cbmc_$(VERSION).orig-java-models-library.tar.gz

.PHONY: build clean default