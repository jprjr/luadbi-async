#include "dbd_mysql.h"

int dbd_mysql_statement_create(lua_State *L, connection_t *conn);

 

/*
 * connection, err = DBD.MySQL.New()
 */
static int connection_new(lua_State *L) {
    connection_t *conn = NULL;
    conn = (connection_t *) lua_newuserdata(L, sizeof(connection_t));
    conn->mysql = mysql_init(NULL);
    if(! conn->mysql ) {
        /* If this happens that's a serious problem! */
        lua_pushnil(L);    
        lua_pushstring(L, DBI_ERR_DB_UNAVAILABLE);    
        return 2;
    }
    /* Enable non-blocking operations */
    /* blocking operations can freely be used */
    mysql_options(conn->mysql, MYSQL_OPT_NONBLOCK, 0);
    luaL_getmetatable(L, DBD_MYSQL_CONNECTION);
    lua_setmetatable(L, -2);
    return 1;
}
    
/*
 * success, err = connection:connect(dbname, user, password, host, port)
 * Connects using a blocking method. Perfect for being lazy.
 */
static int connection_connect(lua_State *L) {
    connection_t *conn = (connection_t *)luaL_checkudata(L, 1, DBD_MYSQL_CONNECTION);
    int n = lua_gettop(L);

    const char *host = NULL;
    const char *user = NULL;
    const char *password = NULL;
    const char *db = NULL;
    int port = 0;

    const char *unix_socket = NULL; 
    int client_flag = 0; /* TODO always 0, set flags from options table */

    if(! conn->mysql ) {
        lua_pushnil(L);    
        lua_pushstring(L, DBI_ERR_DB_UNAVAILABLE);    
        return 2;
    }

    /* db, user, password, host, port */
    switch (n) {
    case 6:
	if (lua_isnil(L, 6) == 0) 
	    port = luaL_checkint(L, 6);
    case 5: 
	if (lua_isnil(L, 5) == 0) 
	    host = luaL_checkstring(L, 5);
    case 4:
	if (lua_isnil(L, 4) == 0) 
	    password = luaL_checkstring(L, 4);
    case 3:
	if (lua_isnil(L, 3) == 0) 
	    user = luaL_checkstring(L, 3);
    case 2:
	/*
	 * db is the only mandatory parameter
	 */
	db = luaL_checkstring(L, 2);
    }

    if(! mysql_real_connect(conn->mysql, host, user, password, db, port, unix_socket, client_flag)) {
        lua_pushnil(L);
        lua_pushfstring(L, DBI_ERR_CONNECTION_FAILED, mysql_error(conn->mysql));
        return 2;
    }
    mysql_autocommit(conn->mysql, 0);

    lua_pushboolean(L, 1);
    return 1;
}

/*
 * success, event = connection:connect_start(dbname, user, password, host, port)
 */
static int connection_connect_start(lua_State *L) {
    int n = lua_gettop(L);
    connection_t *conn = (connection_t *)luaL_checkudata(L, 1, DBD_MYSQL_CONNECTION);

    const char *host = NULL;
    const char *user = NULL;
    const char *password = NULL;
    const char *db = NULL;
    int port = 0;

    const char *unix_socket = NULL; /* TODO always NULL */
    int client_flag = 0; /* TODO always 0, set flags from options table */

    if(! conn->mysql ) {
        lua_pushnil(L);    
        lua_pushstring(L, DBI_ERR_DB_UNAVAILABLE);    
        return 2;
    }

    /* db, user, password, host, port */
    switch (n) {
    case 6:
	if (lua_isnil(L, 6) == 0) 
	    port = luaL_checkint(L, 6);
    case 5: 
	if (lua_isnil(L, 5) == 0) 
	    host = luaL_checkstring(L, 5);
    case 4:
	if (lua_isnil(L, 4) == 0) 
	    password = luaL_checkstring(L, 4);
    case 3:
	if (lua_isnil(L, 3) == 0) 
	    user = luaL_checkstring(L, 3);
    case 2:
	/*
	 * db is the only mandatory parameter
	 */
	db = luaL_checkstring(L, 2);
    }

    mysql_options(conn->mysql, MYSQL_OPT_NONBLOCK, 0);
    conn->event = mysql_real_connect_start(&(conn->ret), conn->mysql, host, user, password, db, port, unix_socket, client_flag);
    /* TODO Check for actual errors (docs don't mention anything about that) */
    if(conn->event == 1) {
        conn->timeout = 1000 * mysql_get_timeout_value_ms(conn->mysql);
    }

    lua_pushboolean(L, 1);
    lua_pushnumber(L, convert_mysql_to_ev(conn->event));

    return 2;
}
/*
 * success, err, event = connection:connect_cont(event)
 * Returns success, error message, next event
 */
static int connection_connect_cont(lua_State *L) {
    connection_t *conn = (connection_t *)luaL_checkudata(L, 1, DBD_MYSQL_CONNECTION);
    int event = convert_ev_to_mysql(lua_tonumber(L, 2));

    if(! conn->mysql ) {
        lua_pushnil(L);    
        lua_pushstring(L, DBI_ERR_DB_UNAVAILABLE);    
        return 2;
    }

    conn->event = mysql_real_connect_cont(&(conn->ret), conn->mysql, event);

    if(conn->event == MYSQL_WAIT_TIMEOUT) {
        conn->timeout = 1000*mysql_get_timeout_value_ms(conn->mysql);
        /* if(conn->timeout == 0) { */
            /* TODO This seems to happen, if I call mysql_real_connect cont continuously it will return 0 */
            /* while( (conn->event = mysql_real_connect_cont(&(conn->ret),conn->mysql,conn->event) ) ) {} */
        /* } */
    }
    if(conn->ret == NULL ) {
        lua_pushnil(L);
        lua_pushfstring(L, DBI_ERR_CONNECTION_FAILED, mysql_error(conn->mysql));
        return 2;
    }
    if(conn->event > 0) {
        lua_pushboolean(L, 0);
        lua_pushnil(L);
        lua_pushnumber(L, convert_mysql_to_ev(conn->event));
        return 2;
    } /* Connection successful */
    /* turn off autocommit */
    /* TODO connect_cont: This is a blocking call */
    mysql_autocommit(conn->mysql,0);
    lua_pushboolean(L,1);
    return 1;
}

/*
 * event = connection:get_event()
 * returns what event to change on (for poll, select, etc)
 */
static int connection_get_event(lua_State *L) {
    connection_t *conn = (connection_t *)luaL_checkudata(L, 1, DBD_MYSQL_CONNECTION);
    lua_pushnumber(L, convert_mysql_to_ev(conn->event) );
    return 1;
}

/*
 * socket = connection:get_socket()
 * returns a file descriptor
 */
static int connection_get_socket(lua_State *L) {
    connection_t *conn = (connection_t *)luaL_checkudata(L, 1, DBD_MYSQL_CONNECTION);
    lua_pushnumber(L, mysql_get_socket(conn->mysql) );
    return 1;
}

/*
 * timeout = connection:get_timeout()
 * returns the current timeout value
 */
static int connection_get_timeout(lua_State *L) {
    connection_t *conn = (connection_t *)luaL_checkudata(L, 1, DBD_MYSQL_CONNECTION);
    lua_pushnumber(L, conn->timeout );
    return 1;
}

/*
 * success = connection:autocommit(on)
 */
static int connection_autocommit(lua_State *L) {
    connection_t *conn = (connection_t *)luaL_checkudata(L, 1, DBD_MYSQL_CONNECTION);
    int on = lua_toboolean(L, 2); 
    int err = 0;

    if (conn->mysql) {
	err = mysql_autocommit(conn->mysql, on);
    }

    lua_pushboolean(L, !err);
    return 1;
}

/*
 * success = connection:close()
 */
static int connection_close(lua_State *L) {
    connection_t *conn = (connection_t *)luaL_checkudata(L, 1, DBD_MYSQL_CONNECTION);
    int disconnect = 0;   

    if (conn->mysql) {
	mysql_close(conn->mysql);
	disconnect = 1;
	conn->mysql = NULL;
    }

    lua_pushboolean(L, disconnect);
    return 1;
}

/*
 * success = connection:commit()
 */
static int connection_commit(lua_State *L) {
    connection_t *conn = (connection_t *)luaL_checkudata(L, 1, DBD_MYSQL_CONNECTION);
    int err = 0;

    if (conn->mysql) {
	err = mysql_commit(conn->mysql);
    }

    lua_pushboolean(L, !err);
    return 1;
}

/* 
 * ok = connection:ping()
 */
static int connection_ping(lua_State *L) {
    connection_t *conn = (connection_t *)luaL_checkudata(L, 1, DBD_MYSQL_CONNECTION);
    int err = 1;   

    if (conn->mysql) {
	err = mysql_ping(conn->mysql);
    }

    lua_pushboolean(L, !err);
    return 1;
}

/*
 * statement,err = connection:create_statement(sql_string)
 */
static int connection_create_statement(lua_State *L) {
    connection_t *conn = (connection_t *)luaL_checkudata(L, 1, DBD_MYSQL_CONNECTION);

    if (conn->mysql) {
	return dbd_mysql_statement_create(L, conn); 
    }

    lua_pushnil(L);    
    lua_pushstring(L, DBI_ERR_DB_UNAVAILABLE);    

    return 2;
}

/*
 * quoted = connection:quote(str)
 */
static int connection_quote(lua_State *L) {
    connection_t *conn = (connection_t *)luaL_checkudata(L, 1, DBD_MYSQL_CONNECTION);
    size_t len;
    const char *from = luaL_checklstring(L, 2, &len);
    char *to = (char *)calloc(len*2+1, sizeof(char));
    int quoted_len;

    if (!conn->mysql) {
        luaL_error(L, DBI_ERR_DB_UNAVAILABLE);
    }

    quoted_len = mysql_real_escape_string(conn->mysql, to, from, len);

    lua_pushlstring(L, to, quoted_len);
    free(to);

    return 1;
}

/*
 * success = connection:rollback()
 */
static int connection_rollback(lua_State *L) {
    connection_t *conn = (connection_t *)luaL_checkudata(L, 1, DBD_MYSQL_CONNECTION);
    int err = 0;

    if (conn->mysql) {
	err = mysql_rollback(conn->mysql);
    }

    lua_pushboolean(L, !err);
    return 1;
}

/*
 * __gc
 */
static int connection_gc(lua_State *L) {
    /* always close the connection */
    connection_close(L);

    return 0;
}

/*
 * __tostring
 */
static int connection_tostring(lua_State *L) {
    connection_t *conn = (connection_t *)luaL_checkudata(L, 1, DBD_MYSQL_CONNECTION);

    lua_pushfstring(L, "%s: %p", DBD_MYSQL_CONNECTION, conn);

    return 1;
}

int dbd_mysql_connection(lua_State *L) {
    static const luaL_Reg connection_methods[] = {
	{"autocommit", connection_autocommit},
	{"close", connection_close},
	{"commit", connection_commit},
	{"connect", connection_connect},
	{"connect_start", connection_connect_start},
	{"connect_cont", connection_connect_cont},
	{"get_event", connection_get_event},
	{"get_socket", connection_get_socket},
	{"get_timeout", connection_get_timeout},
	{"new_statement", connection_create_statement},
	{"ping", connection_ping},
	{"quote", connection_quote},
	{"rollback", connection_rollback},
	{NULL, NULL}
    };

    static const luaL_Reg connection_class_methods[] = {
	{"New", connection_new},
	{NULL, NULL}
    };

    luaL_newmetatable(L, DBD_MYSQL_CONNECTION);
    luaL_register(L, 0, connection_methods);
    lua_pushvalue(L,-1);
    lua_setfield(L, -2, "__index");

    lua_pushcfunction(L, connection_gc);
    lua_setfield(L, -2, "__gc");

    lua_pushcfunction(L, connection_tostring);
    lua_setfield(L, -2, "__tostring");

    luaL_register(L, DBD_MYSQL_CONNECTION, connection_class_methods);

    return 1;    
}

