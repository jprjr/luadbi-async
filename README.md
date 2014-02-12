# luadbi-async

This is a fork of LuaDBI, modified to provide asynchronious database access.

The original LuaDBI is available at http://code.google.com/p/luadbi/

## Usage

The usage for this is a bit different from the normal DBI. The main DBI module has been renamed (I don't want anybody thinking this is a drop-in replacement for luadbi), and connection and statement objects are made explicitly.

```lua
#!/usr/bin/env lua

require "DBIasync";
local luaevent = require "luaevent.core";

local dbh = DBI.New('MySQL');
dbh:connect('dbname','dbuser','dbpass', 'host');
local stmt = dbh:new_statement();

-- silly callback function
function cb(e)
  print("In the cb function");
  local success, err = stmt:execute_cont(e);
  if(success) then
      for row in stmt:rows() do
          print(row[1])
      end
  end
  return luaevent.LEAVE
end

function othercallback(e)
  print("I'll appear after a second!")
end

stmt:prepare("select sleep(6) as awesome");

local success, event = stmt:execute_start();
if success then
  print("Started execute, waiting for data to come in");
  local socket = dbh:get_socket();
  local ebase = luaevent.new();
  ebase:addevent(socket, event, cb);
  ebase:addevent(nil, luaevent.EV_TIMEOUT, othercallback, 1);
  ebase:loop()
end

-- This should output:
-- Started execute, waiting for data to come in
-- I'll appear after a second!
-- In the cb function
-- 0 (the result of calling sleep)
```

Methods like `connect` and `execute` are blocking by default, and operate just as they do in normal luadbi.

Non-blocking equivalents of these functions will have `<function>_start` and `<function>_cont` functions. The `_start` submits a task and returns wait event to wait on. You can then get the socket in use by your connection, and link it all together with your event-y framework. `<function>_cont` will return any errors encountered.

## Requirements

Right now, the MySQL client libraries don't have async methods, but MariaDB does. So this *requires* a MariaDB client.

The "MariaDB Client Library for C" package doesn't support the async methods, so it's necessary to download the full MariaDB package and build the client from that.

If your distro has MariaDB client and header packages available, I suggest using that.

Please note that the MariaDB client can still connect to MySQL server - I've been testing this against an install of MySQL 5.1.

## STATUS

### MySQL: async connect, prepare, and execute is ~~mostly done~~ working
### PostgreSQL: not started
