
# CBMC Windows Installer

This builds a bare-bones Windows Installer for CBMC.

* Download WiX Toolset from wixtoolset.org. Current version is WiX v3.11.2.  Choose wix311.exe
* Enable .NET Framework 3.5 Features
    * Click Start, type "windows features", click on "Turn Windows features on or off"
	* Wait for the "Add Roles and Features Wizard" to start
	* Click "next" for each step until you get to "Features"
	* Select ".NET Framework 3.5 Features" and click Install
* Run wix311.exe
* set PATH="\Program Files (x86)\WiX Toolset v3.11\bin";%PATH%
* Build the MSI
    * candle -arch x64 cbmc.wxs
	* light cbmc.wixobj

The installer is now in cbmc.msi.

Install CBMC with
  * msiexec /i cbmc.msi
  * set PATH="\Program Files\DiffBlue\CBMC 5.12";%PATH%

Test CBMC with
  * cbmc --version


