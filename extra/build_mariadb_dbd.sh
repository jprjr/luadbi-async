#!/usr/bin/env bash
# Example for building the MariaDB driver with a system-installed
# mariadb

set -e
set -x

realpath_portable() {
    [[ $1 = /* ]] && echo "$1" || echo "$PWD/${1#./}"
}

curdir=$(dirname $(realpath_portable "$0"))
lua_version=5.1.5
lua_ver=${lua_version:0:3}
lua_base=${HOME}/.luaenv/versions/${lua_version}
lua_incdir=${lua_base}/include
lua_libdir=${lua_base}/lib
lua_cdir=${lua_base}/lib/lua/${lua_ver}
lua_shdir=${lua_base}/share/lua/${lua_ver}

cd $curdir/..

make mysql CFLAGS="-g -pedantic -Wall -O2 -shared -fpic -I ${lua_incdir} -I /usr/include/mysql -I /usr/include -I ." MYSQL_LDFLAGS="-Wl,-rpath=${lua_libdir} -L${lua_libdir} -L${lua_cdir} -L/usr/lib64 -L/usr/lib -lmysqlclient -llua"
cp DBIasync.lua ${lua_shdir}
cp dbdmysqlasync.so ${lua_cdir}
