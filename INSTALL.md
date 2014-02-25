## Dependencies

Before attempting to build LuaDBI-async the development libaries for each database
must be installed.

 * MariaDB Client Development Files
 * PostgreSQL Development Files
  
## Building 

Run `make` in the source directory to build the database drivers.


## Make Target

 * make all - builds MySQL and PostgreSQL drivers
 * make mysql
 * make psql

## Installation

Please consult your distributions documentation on installing Lua modules.
Please note that both the database binary driver packages (\*.so|,\*.dll) and DBIasync.lua
must be installed correctly to use LuaDBI-async
