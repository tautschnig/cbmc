# CBMC packages

This directory defines GitHub Actions to build CBMC installation
packages for MacOS, Windows, and Ubuntu.

The actions are defined in the yaml file packages.yaml. Each packages
is defined by a job that runs in its own container. The subdirectories
contain files and data needed to build each of the packages.

The MacOS package is just a tar file of a directory containing the
binaries. The directory should be unpacked and placed in the search
path. The Homebrew packages will install the stable releases, and these
tar files will be used for the development versions.

The Windows package is an Microsoft Installer (msi) for Windows 10
with Visual Studio 2019.  It can be installed with `msexec /i <filename>`.

The Ubuntu package is a Debian package that can be installed with
`dpkg -i <filename>`. There are packages for Ubuntu 18 and Ubuntu 16.
The Debian and Ubuntu repositories will install the stable releases,
and these debian packages will be used for the development versions.
