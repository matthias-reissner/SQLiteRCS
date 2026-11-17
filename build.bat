@echo off
setlocal
if not defined SQLITE_INCLUDE_DIR (
  echo Set SQLITE_INCLUDE_DIR to the directory containing sqlite3ext.h.
  exit /b 1
)
cl /nologo /W4 /LD /I"%SQLITE_INCLUDE_DIR%" sqlite_rcs.c /link /OUT:sqlitercs.dll
exit /b %ERRORLEVEL%
