package = "luadbi-async-mysql"
version = "20140222-1"

description = {
	summary = "Database abstraction layer, asynchronous",
	detailed = [[
		This is a fork of LuaDBI that provides asynchronous operations.

		This rock is the MySQL DBD module. You will also need the
		base DBI module to use this software.
	]],
	
	license = "MIT/X11",
	homepage = "http://github.com/jprjr/luadbi-async"
}

source = {
	url = "http://github.com/jprjr/luadbi-async/archive/master.tar.gz",
	dir = "luadbi-async-master"
}

dependencies = {
	"lua >= 5.1",
	"luadbi-async = 20140222"
}

external_dependencies = {
	MYSQL = { header = "mysql.h"  },
    MARIADB = { header = "private/maria.h" },
}

build = {
	type = "builtin",
	modules = {
		['dbd.mysqlasync'] = {
			sources = {
				'dbd/common.c',
				'dbd/mysql/main.c',
				'dbd/mysql/statement.c',
				'dbd/mysql/connection.c'
			},
		
			libraries = {
				'mysqlclient'
			},
			
			incdirs = {
				"$(MYSQL_INCDIR)",
				'./'
			},
			
			libdirs = {
				"$(MYSQL_LIBDIR)"
			}
		}
	}
}
