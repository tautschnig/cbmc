# CBMC Homebrew package

This contains a bare-bones Homebrew formula cbmc.rb for CBMC.

Add the formula to the local tap:
* cp cbmc.rb /usr/local/Homebrew/Library/Taps/homebrew/homebrew-core/Formula
* brew install cbmc

Add the forula to the Homebrew Core tap:
* cp cbmc.rb /usr/local/Homebrew/Library/Taps/homebrew/homebrew-core/Formula
* brew audit --strict --online cbmc
* brew install --build-from-source cbmc
* In the Formula directory confirm, this is a git repository for Homebrew/homebrew master branch
* On GitHub, fork Homebrew/homebrew-core, add to your remotes
* Create a branch of master, add cbmc.rb, commit and push to your fork
* Create a pull request against homebrew master branch
