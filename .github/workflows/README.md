# CBMC packages

This directory defines GitHub Actions to build CBMC installation
packages for MacOS, Windows, and Ubuntu.

The actions are defined in the yaml file packages.yaml. Each package
is defined by a job that runs in its own container. The subdirectories
contain files and data needed to build each of the packages.

The MacOS package is just a tar file of a directory containing the
binaries. The directory should be unpacked and placed in the search
path. Using Homebrew, "brew install cbmc" will install the latest
stable release.  These tar files are intended only to distribute the
development versions between stable releases (Homebrew repository
updates of the stable versions are quick).

The Windows package is an Microsoft Installer (msi) for Windows 10
with Visual Studio 2019.  It can be installed with `msexec /i <filename>`.

The Ubuntu package is a Debian package that can be installed with
`dpkg -i <filename>`. There are packages for Ubuntu 18 and Ubuntu 16.
Using the Debian and Ubuntu repositories, "apt-get install cbmc" will
install a stable release (but 5.10 and not 5.12 as of April 2020).
These packages are intended to distribute the development versions
between stable releases, but can also be used to install stable
releases (Debian repository updates can take months).

Note: As of April 2020, the Debian package is currently just a
bare-bones installer that contains nothing but the binaries and is not
suitable for submission to Debian.  A full Debian package is in the
works.
