# luadbi-async

This is a fork of LuaDBI, modified to provide asynchronious database access.

The original LuaDBI is available at http://code.google.com/p/luadbi/

## Requirements

Right now, the MySQL client libraries don't have async methods, but MariaDB does. So this *requires* a MariaDB client.

The "MariaDB Client Library for C" package doesn't support the async methods, so it's necessary to download the full MariaDB package and build the client from that.

## STATUS

### MySQL: async connect, prepare, and execute is mostly done
### PostgreSQL: not started
