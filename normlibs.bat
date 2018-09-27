FOR /F "delims=" %%i IN ('gcc -dumpmachine') DO set GCCVAR=%%i
set LIBDIR=lib\%GCCVAR%
echo LIBDIR=%LIBDIR%
cd %LIBDIR%
copy /y libsqldb-0.1.1.a   libxc.a
copy /y libsqldb-0.1.1d.a  libxcd.a
copy /y libsqldb-0.1.1d.dll libxcd.dll
copy /y libsqldb-0.1.1.dll  libxc.dll
copy /y *.* ..
cd ../..

