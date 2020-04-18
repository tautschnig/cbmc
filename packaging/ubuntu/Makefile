################################################################
# Configuration

# Path to the git repository
GIT_REPO=https://github.com/diffblue/cbmc
GIT_DIR=/tmp/cbmc

# Package version number (cbmc version number)
VERSION=5.12

# Debian revision number
REVISION=1

# The tag or sha for the git commit used to generate the package
COMMIT_TAG=
COMMIT_SHA=

################################################################
# Definition of the Debian package

# Debian package directory
PKG_DIR=cbmc

BINDIR = $(PKG_DIR)/usr/bin
BINARIES = \
	$(BINDIR)/cbmc \
	$(BINDIR)/goto-analyzer \
	$(BINDIR)/goto-cc \
	$(BINDIR)/goto-diff \
	$(BINDIR)/goto-gcc \
	$(BINDIR)/goto-harness \
	$(BINDIR)/goto-instrument \

MANDIR = $(PKG_DIR)/usr/share/man/man1
MANPAGES = \
	$(MANDIR)/cbmc.1 \

DEBDIR = $(PKG_DIR)/DEBIAN
DEBS = \
	$(DEBDIR)/control \

# Debian package file name
DEB_TAG_WORDS = $(VERSION) $(REVISION) $(COMMIT_TAG) $(COMMIT_SHA)
DEB_TAG = $(shell echo $(DEB_TAG_WORDS) | sed 's/ /-/g')
DEB=cbmc_$(DEB_TAG)_amd64.deb

################################################################

default: $(DEB)

# Run "make $(GIT_DIR)" to clone and build cbmc if needed
$(GIT_DIR):
	mkdir -p $(GIT_DIR)
	cd $(dir $(GIT_DIR)); \
	  git clone $(GIT_REPO) $(notdir $(GIT_DIR))
	cd $(GIT_DIR); \
	  git checkout $(firstword $(COMMIT_TAG) $(COMMIT_SHA) develop)
	cd $(GIT_DIR); \
	  cmake -S. -Bbuild -DWITH_JBMC=NO -GNinja; \
	  cmake --build build

$(BINDIR)/%: $(GIT_DIR)/build/bin/%
	cp $< $@

$(MANDIR)/%: $(GIT_DIR)/doc/man/%
	cp $< $@

$(DEBDIR)/%: %
	SIZE=$(firstword $(shell du -k $(BINDIR))); \
	  cat $< | sed 's/VERSION/$(DEB_TAG)/' | sed 's/SIZE/$(SIZE)/' > $@

$(BINDIR) $(MANDIR) $(DEBDIR):
	mkdir -p $@

$(DEB): $(BINDIR) $(MANDIR) $(DEBDIR) $(BINARIES) $(MANPAGES) $(DEBS)
	dpkg-deb --build $(PKG_DIR) $@

clean:
	$(RM) -r $(PKG_DIR)
	$(RM) $(DEB)

veryclean: clean
	$(RM) -r $(GIT_DIR)
