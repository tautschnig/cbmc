# CBMC Debian/Ubuntu package

This builds a bare-bones Debian package for CBMC.

* Run `make` to
    * Clone and build CBMC in GIT_DIR if it does not already exist.
	* Create a Debian package named `cbmc_5.12_amd64.deb` fro GIT_DIR.

Install with `sudo dpkg -i cbmc_5.12_amd64.deb`.

Remove with `sudo dpkg -r cbmc`.
