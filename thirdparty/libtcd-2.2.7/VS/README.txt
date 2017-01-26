2014-10-25

Building libtcd with Visual Studio

If Cygwin is installed it is possible to run the original configure script
and GNU make as described in Section II below.  build.bat is a shortcut that
does not require Cygwin.  It uses pre-generated tcd.h and defines that
resulted from running configure with Visual Studio Express 2013 for Windows
Desktop Update 3.


I.  Building with build.bat

1.  Do Start -> Visual Studio 2013 -> Visual Studio Tools -> VS2013 x64 Cross
Tools Command Prompt (or VS2013 x86 Native Tools Command Prompt).  To work
around file system permissions problems, right click it and run as
Administrator.

2.  VS\build.bat.

3.  Copy tcd.h and tcd.lib to the desired install directories.


II.  Using Cygwin to run configure + make for VS2013

1.  Get a Visual Studio command prompt as in step I.1 above.

2.  At the command prompt, execute C:\cygwin64\Cygwin.bat (or
C:\cygwin\Cygwin.bat, or substitute the correct path to your Cygwin.bat
file).

3.  Configure libtcd with ./configure CC="cl /O2" CXX="cl /O2" LD="cl /O2"
CPP="cl /E".

4.  make.

You will get a lot of warnings as Visual C++ ignores most of the standard
command-line switches that the autoconf build process tries to use.
