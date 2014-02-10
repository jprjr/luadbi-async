#!/usr/bin/env bash
# Downloads and builds mariadb-client
# required - cmake

set -e
set -x

realpath_portable() {
    [[ $1 = /* ]] && echo "$1" || echo "$PWD/${1#./}"
}

curdir=$(dirname $(realpath_portable "$0"))

maria_version=5.5.35

mkdir -p $curdir/../build
cd $curdir/../build

wget http://ftp.osuosl.org/pub/mariadb/mariadb-${maria_version}/kvm-tarbake-jaunty-x86/mariadb-${maria_version}.tar.gz
tar xf mariadb-${maria_version}.tar.gz
cd mariadb-${maria_version}
cmake . -DWITH_PIC=ON -DDISABLE_SHARED=ON -DWITH_SSL=system -DWITH_ZLIB=system -DCMAKE_BUILD_TYPE=Release
make mysqlclient
