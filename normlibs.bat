FOR /F "delims=" %%i IN ('gcc -dumpmachine') DO set GCCVAR=%%i
set LIBDIR=lib\%GCCVAR%
echo LIBDIR=%LIBDIR%
cd %LIBDIR%
copy /y libsqldb-0.1.2.a   libsqldb.a
copy /y libsqldb-0.1.2d.a  libsqldbd.a
copy /y libsqldb-0.1.2d.dll libsqldbd.dll
copy /y libsqldb-0.1.2.dll  libsqldb.dll
copy /y *.* ..
cd ../..

