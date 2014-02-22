package = "luadbi-async"
version = "20140222-1"

description = {
	summary = "Database abstraction layer, asynchronous",
	detailed = [[
        This is a fork of LuaDBI that provides asynchronous operations.
	]],
	
	license = "MIT/X11",
	homepage = "http://github.com/jprjr/luadbi-async"
}

source = {
	url = "http://github.com/jprjr/luadbi-async/archive/master.tar.gz",
	dir = "luadbi-async-master"
}

dependencies = {
	"lua >= 5.1"
}

build = {
	type = "builtin",
	modules = {
		["DBIasync"] = "DBIasync.lua"
	}
}
