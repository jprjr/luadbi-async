#!/usr/bin/env bash
# Example for building the MariaDB driver with the bundled MariaDB

set -e
set -x

realpath_portable() {
    [[ $1 = /* ]] && echo "$1" || echo "$PWD/${1#./}"
}

curdir=$(dirname $(realpath_portable "$0"))
maria_version=5.5.35
lua_incdir=/usr/include/lua5.1

cd $curdir/..

make mysql CFLAGS="-g -pedantic -Wall -O2 -shared -fpic -I ${lua_incdir} -I./build/mariadb-${maria_version}/include -I ." MYSQL_LDFLAGS="-L./build/mariadb-${maria_version}/libmysql/ -L/usr/lib64 -L/usr/lib -lmysqlclient -lssl -lm -lrt -llua"

