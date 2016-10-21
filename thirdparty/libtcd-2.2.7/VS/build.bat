REM Run this from Visual Studio x64 Cross Tools Command Prompt.
REM See VS\README.txt for details.
COPY VS\tcd.h .
CL /O2 /MD /analyze- /DHAVE_IO_H=1 /Dftruncate=_chsize /I. /c bit_pack.c tide_db.c
LIB /SUBSYSTEM:CONSOLE /OUT:tcd.lib bit_pack.obj tide_db.obj
