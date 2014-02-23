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

Non-blocking equivalents of these functions will have `<function>_start` and `<function>_cont` functions. The `_start` submits a task and returns what event to wait on. You can then get the socket in use by your connection, and link it all together with your event-y framework. `<function>_cont` will return any errors encountered. If the `event` returned is zero, that means the method completed without blocking.

The `_start` functions generally return `success, err/event` to indicate if the function was submitted successfully, and what event to wait on (such as `EV_READ` or `EV_TIMEOUT`). The `_cont` functions generally return `success, err/event, (data)` - in the case of an error, `success` will be `nil`. If the database indicates there's some event it's still waiting, `success` will be `false` and `event` will be set. If `success` is `true`, everything completed normally.

## Requirements

Right now, the MySQL client libraries don't have (documented) async methods, but MariaDB does. So this *requires* the MariaDB client, it shouldn't compile against MySQL.

The "MariaDB Client Library for C" package doesn't support the async methods, so it's necessary to download the full MariaDB package and build the client from that.

If your distro has MariaDB client and header packages available, I suggest using that.

Please note that the MariaDB client can still connect to MySQL server - I've been testing this against an install of MySQL 5.1.

## Methods/Objects

All these methods are completed for MySQL.

### DBI Object

`DBI` (object)

On requiring DBIasync, a new global object DBI is registered (expect this to change soon - instead of registering a global object, the syntax will be somethnig more like `local dbi = require "DBIasync"`

`connection = DBI.New(<driver>)` 

Creates a new `connection` object with your selected driver. Available drivers are `MySQL` (PostgreSQL has a ways for me to go).

### Connection Object

#### Non-blocking methods

The following methods do not block (mostly).

`success, err/event = connection:connect_start(dbname, user, password, host, port)`

Opens a connection to a database, non-blocking.

`success, err/event = connection:connect_cont(event)`

Finish with opening a connection to a database. The `event` passed to the function should be whatever event was triggered.

Technically, this does block (once the connection is successful, autocommit is disabled using a blocking method), I don't know if it's worth changing this.

`socket = connection:get_socket()`

Returns the socket file descriptor used by the underlying connection.

`timeout = connection:get_timeout()`

In the case of a returned `event` being `EV_TIMEOUT`, this function will return the current timeout, in seconds.

`statement = connection:create_statement()`

Returns a new `statement` object, for preparing and executing queries.

`quoted = connection:quote(str)`

Returns an escaped string.

#### Blocking methods

`success, err = connection:connect(dbname, user, password, host, port)`

Opens up a connection to a database.

`success = connection:autocommit(true/false)`

Enables/disables autocommit.

`success = connection:close()`

Closes the connetion

`success = connection:commit()`

Performs a commit.

`success = connection:rollback()`

Performs a rollback

`ok = connection:ping()`

Performs a SQL ping.

### Statement Object

#### Non-blocking methods

`success, err/event = statement:prepare_start(query)`

Starts preparing a statement.

`success, err/event = statement:prepare_cont(query)`

Finishes preparing a statement.

`success, err/event = statement:execute_start(bindvars)`

Starts executing a statement.

`success, err/event = statement:execute_cont(event)`

Finish executing a statement.

`success, err/event = statement:fetch_start()`

Start fetching a row

`success, err/event/row = statement:fetch_cont(event, true/false)`

Access a fetched row. Pass true/false to enable named indexes. `row` will be `nil` when there's no more rows.

`column_names = statement:columns()`

#### Blocking methods

`success, err = statement:prepare(query)`

`success, err = statement:execute(bindvars)`

`row = statement:fetch(true/false)`

Fetch a row, pass true/false to enable named indexes.

`success = statement:close()`

`rowcount = statement:rowcount()`

Note! This only works after using a blocking `statement:execute` - that method stores the entire result. The non-blocking version doesn't do that, so you'll have to keep count of the rows yourself.

`iter_function = statement:rows(true/false)`

Returns an iterator function to go over all rows (which requires fetching all the rows, heads-up!). Pass true/false to enable named indexes.

## STATUS

### MySQL: The more important async bits are complete.
### PostgreSQL: not started
