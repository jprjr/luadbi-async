#include "dbd_mysql.h"

static lua_push_type_t
mysql_to_lua_push (unsigned int mysql_type) /* {{{ */
{
  lua_push_type_t lua_type;

  switch (mysql_type)
    {
    case MYSQL_TYPE_NULL:
      lua_type = LUA_PUSH_NIL;
      break;

    case MYSQL_TYPE_TINY:
    case MYSQL_TYPE_YEAR:
    case MYSQL_TYPE_SHORT:
    case MYSQL_TYPE_LONG:
      lua_type = LUA_PUSH_INTEGER;
      break;

    case MYSQL_TYPE_DOUBLE:
    case MYSQL_TYPE_LONGLONG:
      lua_type = LUA_PUSH_NUMBER;
      break;

    default:
      lua_type = LUA_PUSH_STRING;
    }

  return lua_type;
} /* }}} */

static size_t
mysql_buffer_size (MYSQL_FIELD * field) /* {{{ */
{
  unsigned int mysql_type = field->type;
  size_t size = 0;

  switch (mysql_type)
    {
    case MYSQL_TYPE_TINY:
      size = 1;
      break;
    case MYSQL_TYPE_YEAR:
    case MYSQL_TYPE_SHORT:
      size = 2;
      break;
    case MYSQL_TYPE_INT24:
      size = 4;
      break;
    case MYSQL_TYPE_LONG:
      size = 4;
      break;
    case MYSQL_TYPE_LONGLONG:
      size = 8;
      break;
    case MYSQL_TYPE_FLOAT:
      size = 4;
      break;
    case MYSQL_TYPE_DOUBLE:
      size = 8;
      break;
    case MYSQL_TYPE_TIME:
    case MYSQL_TYPE_DATE:
    case MYSQL_TYPE_DATETIME:
    case MYSQL_TYPE_TIMESTAMP:
      size = sizeof (MYSQL_TIME);
      break;
    default:
      size = field->length;
    }

  return size;
} /* }}} */

/*
 * num_affected_rows = statement:affected()
 */
static int
statement_affected (lua_State * L) /* {{{ */
{
  statement_t *statement =
    (statement_t *) luaL_checkudata (L, 1, DBD_MYSQL_STATEMENT);

  if (!statement->stmt)
    {
      luaL_error (L, DBI_ERR_INVALID_STATEMENT);
    }

  lua_pushinteger (L, mysql_stmt_affected_rows (statement->stmt));

  return 1;
} /* }}} */

/*
 * success = statement:close()
 */
static int
statement_close (lua_State * L) /* {{{ */
{
  statement_t *statement =
    (statement_t *) luaL_checkudata (L, 1, DBD_MYSQL_STATEMENT);

  if (statement->metadata)
    {
      mysql_free_result (statement->metadata);
      statement->metadata = NULL;
    }

  if (statement->stmt)
    {
      mysql_stmt_close (statement->stmt);
      statement->stmt = NULL;
    }

  lua_pushboolean (L, 1);
  return 1;
} /* }}} */

/*
 * column_names = statement:columns()
 */
static int
statement_columns (lua_State * L) /* {{{ */
{
  statement_t *statement =
    (statement_t *) luaL_checkudata (L, 1, DBD_MYSQL_STATEMENT);

  MYSQL_FIELD *fields;
  int i;
  int num_columns;
  int d = 1;

  if (!statement->stmt)
    {
      luaL_error (L, DBI_ERR_INVALID_STATEMENT);
      return 0;
    }

  fields = mysql_fetch_fields (statement->metadata);
  num_columns = mysql_num_fields (statement->metadata);
  lua_newtable (L);
  for (i = 0; i < num_columns; i++)
    {
      const char *name = fields[i].name;

      LUA_PUSH_ARRAY_STRING (d, name);
    }

  return 1;
} /* }}} */

/*
 * success,event = statement:execute(...)
 */
static int
statement_execute_start (lua_State * L) /* {{{ */
{
  int n = lua_gettop (L);
  statement_t *statement =
    (statement_t *) luaL_checkudata (L, 1, DBD_MYSQL_STATEMENT);
  int num_bind_params = n - 1;
  int expected_params;

  unsigned char *buffer = NULL;
  int offset = 0;

  char *error_message = NULL;
  char *errstr = NULL;

  int p;

  if (statement->metadata)
    {
      /*
       * free existing metadata from any previous executions
       * TODO mysql_free_result is blocking
       */
      mysql_free_result (statement->metadata);
      statement->metadata = NULL;
    }

  if (!statement->stmt)
    {
      lua_pushboolean (L, 0);
      lua_pushstring (L, DBI_ERR_EXECUTE_INVALID);
      return 2;
    }

  expected_params = mysql_stmt_param_count (statement->stmt);

  if (expected_params != num_bind_params)
    {
      /*
       * mysql_stmt_bind_param does not handle this condition,
       * and the client library will segfault if these do no match
       */
      lua_pushboolean (L, 0);
      lua_pushfstring (L, DBI_ERR_PARAM_MISCOUNT, expected_params,
		       num_bind_params);
      return 2;
    }

  if (num_bind_params > 0)
    {
      statement->bind = malloc (sizeof (MYSQL_BIND) * num_bind_params);
      if (statement->bind == NULL)
	{
	  luaL_error (L, "Could not alloc bind params\n");
	}

      buffer = (unsigned char *) malloc (num_bind_params * sizeof (double));
      memset (statement->bind, 0, sizeof (MYSQL_BIND) * num_bind_params);
    }

  for (p = 2; p <= n; p++)
    {				/* {{{ */
      int type = lua_type (L, p);
      int i = p - 2;

      const char *str = NULL;
      size_t *str_len = NULL;
      double *num = NULL;
      int *boolean = NULL;
      char err[64];

      switch (type)
	{
	case LUA_TNIL:
	  statement->bind[i].buffer_type = MYSQL_TYPE_NULL;
	  statement->bind[i].is_null = (my_bool *) 1;
	  break;

	case LUA_TBOOLEAN:
	  boolean = (int *) (buffer + offset);
	  offset += sizeof (int);
	  *boolean = lua_toboolean (L, p);

	  statement->bind[i].buffer_type = MYSQL_TYPE_LONG;
	  statement->bind[i].is_null = (my_bool *) 0;
	  statement->bind[i].buffer = (char *) boolean;
	  statement->bind[i].length = 0;
	  break;

	case LUA_TNUMBER:
	  /*
	   * num needs to be it's own 
	   * memory here
	   */
	  num = (double *) (buffer + offset);
	  offset += sizeof (double);
	  *num = lua_tonumber (L, p);

	  statement->bind[i].buffer_type = MYSQL_TYPE_DOUBLE;
	  statement->bind[i].is_null = (my_bool *) 0;
	  statement->bind[i].buffer = (char *) num;
	  statement->bind[i].length = 0;
	  break;

	case LUA_TSTRING:
	  str_len = (size_t *) (buffer + offset);
	  offset += sizeof (size_t);
	  str = lua_tolstring (L, p, str_len);

	  statement->bind[i].buffer_type = MYSQL_TYPE_STRING;
	  statement->bind[i].is_null = (my_bool *) 0;
	  statement->bind[i].buffer = (char *) str;
	  statement->bind[i].length = str_len;
	  break;

	default:
	  snprintf (err, sizeof (err) - 1, DBI_ERR_BINDING_TYPE_ERR,
		    lua_typename (L, type));
	  errstr = err;
	  error_message = DBI_ERR_BINDING_PARAMS;
	  goto cleanup;
	}
    }				/* }}} */

  if (mysql_stmt_bind_param (statement->stmt, statement->bind))
    {
      error_message = DBI_ERR_BINDING_PARAMS;
      goto cleanup;
    }

  statement->event =
    mysql_stmt_execute_start (&(statement->ret), statement->stmt);

  if (statement->event & MYSQL_WAIT_EXCEPT)
    {
      lua_pushnil (L);
      lua_pushfstring (L, DBI_ERR_BINDING_EXEC,
		       mysql_stmt_error (statement->stmt));
    }

  if (statement->event & MYSQL_WAIT_TIMEOUT)
    {
      statement->timeout =
	1000 * mysql_get_timeout_value_ms (statement->mysql);
    }

cleanup:
  if (statement->bind)
    {
      free (statement->bind);
    }

  if (buffer)
    {
      free (buffer);
    }

  if (error_message)
    {
      lua_pushboolean (L, 0);
      lua_pushfstring (L, error_message,
		       errstr ? errstr : mysql_stmt_error (statement->stmt));
      return 2;
    }

  lua_pushboolean (L, 1);
  lua_pushnumber (L, convert_mysql_to_ev (statement->event));
  return 2;
} /* }}} */

/*
 * success,err = statement:execute_cont(...)
 */
static int
statement_execute_cont (lua_State * L) /* {{{ */
{
  statement_t *statement =
    (statement_t *) luaL_checkudata (L, 1, DBD_MYSQL_STATEMENT);
  int event = convert_ev_to_mysql (lua_tonumber (L, 2));
  MYSQL_RES *metadata = NULL;

  if (!statement->stmt)
    {
      luaL_error (L, DBI_ERR_INVALID_STATEMENT);
    }
  statement->event =
    mysql_stmt_execute_cont (&(statement->ret), statement->stmt, event);

  if (statement->event & MYSQL_WAIT_EXCEPT)
    {
      lua_pushnil (L);
      lua_pushfstring (L, DBI_ERR_BINDING_EXEC,
		       mysql_stmt_error (statement->stmt));
    }

  if (statement->event & MYSQL_WAIT_TIMEOUT)
    {
      statement->timeout =
	1000 * mysql_get_timeout_value_ms (statement->mysql);
    }
  if (statement->ret)
    {
      lua_pushnil (L);
      lua_pushfstring (L, DBI_ERR_BINDING_EXEC,
		       mysql_stmt_error (statement->stmt));
      return 2;
    }
  if (statement->event > 0)
    {
      lua_pushboolean (L, 0);
      lua_pushnumber (L, convert_mysql_to_ev (statement->event));
      return 2;
    }

  metadata = mysql_stmt_result_metadata (statement->stmt);

  statement->metadata = metadata;

  lua_pushboolean (L, 1);
  return 1;
} /* }}} */



/*
 * success,err = statement:execute(...)
 */
static int
statement_execute (lua_State * L) /* {{{ */
{
  int n = lua_gettop (L);
  statement_t *statement =
    (statement_t *) luaL_checkudata (L, 1, DBD_MYSQL_STATEMENT);
  int num_bind_params = n - 1;
  int expected_params;

  unsigned char *buffer = NULL;
  int offset = 0;

  MYSQL_BIND *bind = NULL;
  MYSQL_RES *metadata = NULL;

  char *error_message = NULL;
  char *errstr = NULL;

  int p;

  if (statement->metadata)
    {
      /*
       * free existing metadata from any previous executions
       */
      mysql_free_result (statement->metadata);
      statement->metadata = NULL;
    }

  if (!statement->stmt)
    {
      lua_pushboolean (L, 0);
      lua_pushstring (L, DBI_ERR_EXECUTE_INVALID);
      return 2;
    }

  expected_params = mysql_stmt_param_count (statement->stmt);

  if (expected_params != num_bind_params)
    {
      /*
       * mysql_stmt_bind_param does not handle this condition,
       * and the client library will segfault if these do no match
       */
      lua_pushboolean (L, 0);
      lua_pushfstring (L, DBI_ERR_PARAM_MISCOUNT, expected_params,
		       num_bind_params);
      return 2;
    }

  if (num_bind_params > 0)
    {
      bind = malloc (sizeof (MYSQL_BIND) * num_bind_params);
      if (bind == NULL)
	{
	  luaL_error (L, "Could not alloc bind params\n");
	}

      buffer = (unsigned char *) malloc (num_bind_params * sizeof (double));
      memset (bind, 0, sizeof (MYSQL_BIND) * num_bind_params);
    }

  for (p = 2; p <= n; p++)
    {
      int type = lua_type (L, p);
      int i = p - 2;

      const char *str = NULL;
      size_t *str_len = NULL;
      double *num = NULL;
      int *boolean = NULL;
      char err[64];

      switch (type)
	{
	case LUA_TNIL:
	  bind[i].buffer_type = MYSQL_TYPE_NULL;
	  bind[i].is_null = (my_bool *) 1;
	  break;

	case LUA_TBOOLEAN:
	  boolean = (int *) (buffer + offset);
	  offset += sizeof (int);
	  *boolean = lua_toboolean (L, p);

	  bind[i].buffer_type = MYSQL_TYPE_LONG;
	  bind[i].is_null = (my_bool *) 0;
	  bind[i].buffer = (char *) boolean;
	  bind[i].length = 0;
	  break;

	case LUA_TNUMBER:
	  /*
	   * num needs to be it's own 
	   * memory here
	   */
	  num = (double *) (buffer + offset);
	  offset += sizeof (double);
	  *num = lua_tonumber (L, p);

	  bind[i].buffer_type = MYSQL_TYPE_DOUBLE;
	  bind[i].is_null = (my_bool *) 0;
	  bind[i].buffer = (char *) num;
	  bind[i].length = 0;
	  break;

	case LUA_TSTRING:
	  str_len = (size_t *) (buffer + offset);
	  offset += sizeof (size_t);
	  str = lua_tolstring (L, p, str_len);

	  bind[i].buffer_type = MYSQL_TYPE_STRING;
	  bind[i].is_null = (my_bool *) 0;
	  bind[i].buffer = (char *) str;
	  bind[i].length = str_len;
	  break;

	default:
	  snprintf (err, sizeof (err) - 1, DBI_ERR_BINDING_TYPE_ERR,
		    lua_typename (L, type));
	  errstr = err;
	  error_message = DBI_ERR_BINDING_PARAMS;
	  goto cleanup;
	}
    }

  if (mysql_stmt_bind_param (statement->stmt, bind))
    {
      error_message = DBI_ERR_BINDING_PARAMS;
      goto cleanup;
    }

  if (mysql_stmt_execute (statement->stmt))
    {
      error_message = DBI_ERR_BINDING_EXEC;
      goto cleanup;
    }

  metadata = mysql_stmt_result_metadata (statement->stmt);

  if (metadata)
    {
      mysql_stmt_store_result (statement->stmt);
    }

cleanup:
  if (bind)
    {
      free (bind);
    }

  if (buffer)
    {
      free (buffer);
    }

  if (error_message)
    {
      lua_pushboolean (L, 0);
      lua_pushfstring (L, error_message,
		       errstr ? errstr : mysql_stmt_error (statement->stmt));
      return 2;
    }

  statement->metadata = metadata;

  lua_pushboolean (L, 1);
  return 1;
} /* }}} */

static int
statement_fetch_impl_start (lua_State * L, statement_t * statement) /* {{{ */
{
  int column_count;
  unsigned long *real_length = NULL;
  const char *error_message = NULL;

  if (!statement->stmt)
    {
      luaL_error (L, DBI_ERR_FETCH_INVALID);
      return 0;
    }

  if (!statement->metadata)
    {
      luaL_error (L, DBI_ERR_FETCH_NO_EXECUTE);
      return 0;
    }

  column_count = mysql_num_fields (statement->metadata);

  if (column_count > 0)
    {
      int i;

      real_length = calloc (column_count, sizeof (unsigned long));

      statement->bind = malloc (sizeof (MYSQL_BIND) * column_count);
      memset (statement->bind, 0, sizeof (MYSQL_BIND) * column_count);

      statement->fields = mysql_fetch_fields (statement->metadata);

      for (i = 0; i < column_count; i++)
	{
	  unsigned int length = mysql_buffer_size (&statement->fields[i]);
	  if (length > sizeof (MYSQL_TIME))
	    {
	      statement->bind[i].buffer = NULL;
	      statement->bind[i].buffer_length = 0;
	    }
	  else
	    {
	      char *buffer = (char *) malloc (length);
	      memset (buffer, 0, length);

	      statement->bind[i].buffer = buffer;
	      statement->bind[i].buffer_length = length;
	    }

	  statement->bind[i].buffer_type = statement->fields[i].type;
	  statement->bind[i].length = &real_length[i];
	}

      if (mysql_stmt_bind_result (statement->stmt, statement->bind))
	{
	  error_message = DBI_ERR_BINDING_RESULTS;
	  goto cleanup;
	}

      statement->event =
	mysql_stmt_fetch_start (&(statement->ret), statement->stmt);

      if (statement->event & MYSQL_WAIT_EXCEPT)
	{
	  lua_pushnil (L);
	  return 1;
	}
      if (statement->event & MYSQL_WAIT_TIMEOUT)
	{
	  statement->timeout =
	    1000 * mysql_get_timeout_value_ms (statement->mysql);
	}

      lua_pushboolean (L, 1);
      lua_pushnumber (L, statement->event);
      return 2;
    }
  else
    {
      lua_pushnil (L);
      return 1;
    }


cleanup:
  free (real_length);

  if (statement->bind)
    {
      int i;

      for (i = 0; i < column_count; i++)
	{
	  free (statement->bind[i].buffer);
	}

      free (statement->bind);
    }


  if (error_message)
    {
      luaL_error (L, error_message, mysql_stmt_error (statement->stmt));
      return 0;
    }

  return 1;
} /* }}} */

static int
statement_fetch_impl_cont (lua_State * L, statement_t * statement, /* {{{ */
			   int event, int named_columns)
{
  int column_count;
  unsigned long *real_length = NULL;
  int values = 0;

  column_count = mysql_num_fields (statement->metadata);

  if (column_count > 0)
    {
      int i;

      real_length = calloc (column_count, sizeof (unsigned long));

      statement->event =
	mysql_stmt_fetch_cont (&(statement->ret), statement->stmt, event);

      if (statement->event & MYSQL_WAIT_EXCEPT)
	{
	  lua_pushnil (L);
	  lua_pushfstring (L, mysql_stmt_error (statement->stmt));
	  values = 2;
	}

      if (statement->event & MYSQL_WAIT_TIMEOUT)
	{
	  statement->timeout =
	    1000 * mysql_get_timeout_value_ms (statement->mysql);
	}
      /* Pretty sure statement->ret should be the return value, but it seems to return 1 */
      /* using mysql_stmt_errno instead */
      if (mysql_stmt_errno (statement->stmt) != 0)
	{
	  lua_pushnil (L);
	  lua_pushstring (L, mysql_stmt_error (statement->stmt));
	  values = 2;
	}
      if (statement->event > 0)
	{
	  lua_pushboolean (L, 0);
	  lua_pushnumber (L, convert_mysql_to_ev (statement->event));
	  values = 2;
	}

      if (mysql_stmt_errno (statement->stmt) == 0
	  || statement->ret == MYSQL_DATA_TRUNCATED)
	{
	  int d = 1;

	  lua_pushboolean (L, 1);
	  lua_newtable (L);
	  values = 2;
	  for (i = 0; i < column_count; i++)
	    {
	      lua_push_type_t lua_push =
		mysql_to_lua_push (statement->fields[i].type);
	      const char *name = statement->fields[i].name;

	      if (statement->bind[i].buffer == NULL)
		{
		  char *buffer =
		    (char *) calloc (real_length[i] + 1, sizeof (char));
		  statement->bind[i].buffer = buffer;
		  statement->bind[i].buffer_length = real_length[i];
		  mysql_stmt_fetch_column (statement->stmt,
					   &(statement->bind[i]), i, 0);
		}

	      if (lua_push == LUA_PUSH_NIL)
		{
		  if (named_columns)
		    {
		      LUA_PUSH_ATTRIB_NIL (name);
		    }
		  else
		    {
		      LUA_PUSH_ARRAY_NIL (d);
		    }
		}
	      else if (lua_push == LUA_PUSH_INTEGER)
		{
		  if (statement->fields[i].type == MYSQL_TYPE_YEAR
		      || statement->fields[i].type == MYSQL_TYPE_SHORT)
		    {
		      if (named_columns)
			{
			  LUA_PUSH_ATTRIB_INT (name,
					       *(short
						 *) (statement->
						     bind[i].buffer));
			}
		      else
			{
			  LUA_PUSH_ARRAY_INT (d,
					      *(short
						*) (statement->
						    bind[i].buffer));
			}
		    }
		  else if (statement->fields[i].type == MYSQL_TYPE_TINY)
		    {
		      if (named_columns)
			{
			  LUA_PUSH_ATTRIB_INT (name,
					       (int) *(char
						       *) (statement->bind
							   [i].buffer));
			}
		      else
			{
			  LUA_PUSH_ARRAY_INT (d,
					      (int) *(char
						      *) (statement->bind
							  [i].buffer));
			}
		    }
		  else
		    {
		      if (named_columns)
			{
			  LUA_PUSH_ATTRIB_INT (name,
					       *(int
						 *) (statement->
						     bind[i].buffer));
			}
		      else
			{
			  LUA_PUSH_ARRAY_INT (d,
					      *(int
						*) (statement->
						    bind[i].buffer));
			}
		    }
		}
	      else if (lua_push == LUA_PUSH_NUMBER)
		{
		  if (named_columns)
		    {
		      LUA_PUSH_ATTRIB_FLOAT (name,
					     *(double
					       *) (statement->
						   bind[i].buffer));
		    }
		  else
		    {
		      LUA_PUSH_ARRAY_FLOAT (d,
					    *(double
					      *) (statement->bind[i].buffer));
		    }
		}
	      else if (lua_push == LUA_PUSH_STRING)
		{

		  if (statement->fields[i].type == MYSQL_TYPE_TIMESTAMP
		      || statement->fields[i].type == MYSQL_TYPE_DATETIME)
		    {
		      char str[20];
		      struct st_mysql_time *t = statement->bind[i].buffer;

		      snprintf (str, 20, "%d-%02d-%02d %02d:%02d:%02d",
				t->year, t->month, t->day, t->hour,
				t->minute, t->second);

		      if (named_columns)
			{
			  LUA_PUSH_ATTRIB_STRING (name, str);
			}
		      else
			{
			  LUA_PUSH_ARRAY_STRING (d, str);
			}
		    }
		  else if (statement->fields[i].type == MYSQL_TYPE_TIME)
		    {
		      char str[9];
		      struct st_mysql_time *t = statement->bind[i].buffer;

		      snprintf (str, 9, "%02d:%02d:%02d", t->hour,
				t->minute, t->second);

		      if (named_columns)
			{
			  LUA_PUSH_ATTRIB_STRING (name, str);
			}
		      else
			{
			  LUA_PUSH_ARRAY_STRING (d, str);
			}
		    }
		  else if (statement->fields[i].type == MYSQL_TYPE_DATE)
		    {
		      char str[20];
		      struct st_mysql_time *t = statement->bind[i].buffer;

		      snprintf (str, 11, "%d-%02d-%02d", t->year,
				t->month, t->day);

		      if (named_columns)
			{
			  LUA_PUSH_ATTRIB_STRING (name, str);
			}
		      else
			{
			  LUA_PUSH_ARRAY_STRING (d, str);
			}

		    }
		  else
		    {
		      if (named_columns)
			{
			  LUA_PUSH_ATTRIB_STRING (name,
						  statement->bind[i].buffer);
			}
		      else
			{
			  LUA_PUSH_ARRAY_STRING (d,
						 statement->bind[i].buffer);
			}
		    }
		}
	      else if (lua_push == LUA_PUSH_BOOLEAN)
		{
		  if (named_columns)
		    {
		      LUA_PUSH_ATTRIB_BOOL (name,
					    *(int *) (statement->
						      bind[i].buffer));
		    }
		  else
		    {
		      LUA_PUSH_ARRAY_BOOL (d,
					   *(int *) (statement->
						     bind[i].buffer));
		    }
		}
	      else
		{
		  luaL_error (L, DBI_ERR_UNKNOWN_PUSH);
		  values = 1;
		}
	    }
	}
      else
	{
	  lua_pushnil (L);
	  values = 1;
	}
    }

  if (statement->bind)
    {
      int i;

      for (i = 0; i < column_count; i++)
	{
	  free (statement->bind[i].buffer);
	}

      free (statement->bind);
    }
  return values;

} /* }}} */

static int
statement_fetch_impl (lua_State * L, statement_t * statement, /* {{{ */
		      int named_columns)
{
  int column_count, fetch_result_ok;
  MYSQL_BIND *bind = NULL;
  unsigned long *real_length = NULL;
  const char *error_message = NULL;

  if (!statement->stmt)
    {
      luaL_error (L, DBI_ERR_FETCH_INVALID);
      return 0;
    }

  if (!statement->metadata)
    {
      luaL_error (L, DBI_ERR_FETCH_NO_EXECUTE);
      return 0;
    }

  column_count = mysql_num_fields (statement->metadata);

  if (column_count > 0)
    {
      int i;
      MYSQL_FIELD *fields;

      real_length = calloc (column_count, sizeof (unsigned long));

      bind = malloc (sizeof (MYSQL_BIND) * column_count);
      memset (bind, 0, sizeof (MYSQL_BIND) * column_count);

      fields = mysql_fetch_fields (statement->metadata);

      for (i = 0; i < column_count; i++)
	{
	  unsigned int length = mysql_buffer_size (&fields[i]);
	  if (length > sizeof (MYSQL_TIME))
	    {
	      bind[i].buffer = NULL;
	      bind[i].buffer_length = 0;
	    }
	  else
	    {
	      char *buffer = (char *) malloc (length);
	      memset (buffer, 0, length);

	      bind[i].buffer = buffer;
	      bind[i].buffer_length = length;
	    }

	  bind[i].buffer_type = fields[i].type;
	  bind[i].length = &real_length[i];
	}

      if (mysql_stmt_bind_result (statement->stmt, bind))
	{
	  error_message = DBI_ERR_BINDING_RESULTS;
	  goto cleanup;
	}

      fetch_result_ok = mysql_stmt_fetch (statement->stmt);
      if (fetch_result_ok == 0 || fetch_result_ok == MYSQL_DATA_TRUNCATED)
	{
	  int d = 1;

	  lua_newtable (L);
	  for (i = 0; i < column_count; i++)
	    {
	      lua_push_type_t lua_push = mysql_to_lua_push (fields[i].type);
	      const char *name = fields[i].name;

	      if (bind[i].buffer == NULL)
		{
		  char *buffer =
		    (char *) calloc (real_length[i] + 1, sizeof (char));
		  bind[i].buffer = buffer;
		  bind[i].buffer_length = real_length[i];
		  mysql_stmt_fetch_column (statement->stmt, &bind[i], i, 0);
		}

	      if (lua_push == LUA_PUSH_NIL)
		{
		  if (named_columns)
		    {
		      LUA_PUSH_ATTRIB_NIL (name);
		    }
		  else
		    {
		      LUA_PUSH_ARRAY_NIL (d);
		    }
		}
	      else if (lua_push == LUA_PUSH_INTEGER)
		{
		  if (fields[i].type == MYSQL_TYPE_YEAR
		      || fields[i].type == MYSQL_TYPE_SHORT)
		    {
		      if (named_columns)
			{
			  LUA_PUSH_ATTRIB_INT (name,
					       *(short *) (bind[i].buffer));
			}
		      else
			{
			  LUA_PUSH_ARRAY_INT (d, *(short *) (bind[i].buffer));
			}
		    }
		  else if (fields[i].type == MYSQL_TYPE_TINY)
		    {
		      if (named_columns)
			{
			  LUA_PUSH_ATTRIB_INT (name,
					       (int) *(char
						       *) (bind[i].buffer));
			}
		      else
			{
			  LUA_PUSH_ARRAY_INT (d,
					      (int) *(char
						      *) (bind[i].buffer));
			}
		    }
		  else
		    {
		      if (named_columns)
			{
			  LUA_PUSH_ATTRIB_INT (name,
					       *(int *) (bind[i].buffer));
			}
		      else
			{
			  LUA_PUSH_ARRAY_INT (d, *(int *) (bind[i].buffer));
			}
		    }
		}
	      else if (lua_push == LUA_PUSH_NUMBER)
		{
		  if (named_columns)
		    {
		      LUA_PUSH_ATTRIB_FLOAT (name,
					     *(double *) (bind[i].buffer));
		    }
		  else
		    {
		      LUA_PUSH_ARRAY_FLOAT (d, *(double *) (bind[i].buffer));
		    }
		}
	      else if (lua_push == LUA_PUSH_STRING)
		{

		  if (fields[i].type == MYSQL_TYPE_TIMESTAMP
		      || fields[i].type == MYSQL_TYPE_DATETIME)
		    {
		      char str[20];
		      struct st_mysql_time *t = bind[i].buffer;

		      snprintf (str, 20, "%d-%02d-%02d %02d:%02d:%02d",
				t->year, t->month, t->day, t->hour,
				t->minute, t->second);

		      if (named_columns)
			{
			  LUA_PUSH_ATTRIB_STRING (name, str);
			}
		      else
			{
			  LUA_PUSH_ARRAY_STRING (d, str);
			}
		    }
		  else if (fields[i].type == MYSQL_TYPE_TIME)
		    {
		      char str[9];
		      struct st_mysql_time *t = bind[i].buffer;

		      snprintf (str, 9, "%02d:%02d:%02d", t->hour,
				t->minute, t->second);

		      if (named_columns)
			{
			  LUA_PUSH_ATTRIB_STRING (name, str);
			}
		      else
			{
			  LUA_PUSH_ARRAY_STRING (d, str);
			}
		    }
		  else if (fields[i].type == MYSQL_TYPE_DATE)
		    {
		      char str[20];
		      struct st_mysql_time *t = bind[i].buffer;

		      snprintf (str, 11, "%d-%02d-%02d", t->year,
				t->month, t->day);

		      if (named_columns)
			{
			  LUA_PUSH_ATTRIB_STRING (name, str);
			}
		      else
			{
			  LUA_PUSH_ARRAY_STRING (d, str);
			}

		    }
		  else
		    {
		      if (named_columns)
			{
			  LUA_PUSH_ATTRIB_STRING (name, bind[i].buffer);
			}
		      else
			{
			  LUA_PUSH_ARRAY_STRING (d, bind[i].buffer);
			}
		    }
		}
	      else if (lua_push == LUA_PUSH_BOOLEAN)
		{
		  if (named_columns)
		    {
		      LUA_PUSH_ATTRIB_BOOL (name, *(int *) (bind[i].buffer));
		    }
		  else
		    {
		      LUA_PUSH_ARRAY_BOOL (d, *(int *) (bind[i].buffer));
		    }
		}
	      else
		{
		  luaL_error (L, DBI_ERR_UNKNOWN_PUSH);
		}
	    }
	}
      else
	{
	  lua_pushnil (L);
	}
    }

cleanup:
  free (real_length);

  if (bind)
    {
      int i;

      for (i = 0; i < column_count; i++)
	{
	  free (bind[i].buffer);
	}

      free (bind);
    }

  if (error_message)
    {
      luaL_error (L, error_message, mysql_stmt_error (statement->stmt));
      return 0;
    }

  return 1;
} /* }}} */

static int
next_iterator (lua_State * L) /* {{{ */
{
  statement_t *statement =
    (statement_t *) luaL_checkudata (L, lua_upvalueindex (1),
				     DBD_MYSQL_STATEMENT);
  int named_columns = lua_toboolean (L, lua_upvalueindex (2));

  return statement_fetch_impl (L, statement, named_columns);
} /* }}} */

/*
 * success,event = statement:fetch_start(named_indexes)
 */
static int
statement_fetch_start (lua_State * L) /* {{{ */
{
  statement_t *statement =
    (statement_t *) luaL_checkudata (L, 1, DBD_MYSQL_STATEMENT);

  return statement_fetch_impl_start (L, statement);
} /* }}} */

/*
 * success,event,table = statement:fetch_cont(event, named_indexes)
 */
static int
statement_fetch_cont (lua_State * L) /* {{{ */
{
  statement_t *statement =
    (statement_t *) luaL_checkudata (L, 1, DBD_MYSQL_STATEMENT);
  int event = convert_ev_to_mysql (lua_tonumber (L, 2));
  int named_columns = lua_toboolean (L, 3);

  return statement_fetch_impl_cont (L, statement, event, named_columns);
} /* }}} */

/*
 * table = statement:fetch(named_indexes)
 */
static int
statement_fetch (lua_State * L) /* {{{ */
{
  statement_t *statement =
    (statement_t *) luaL_checkudata (L, 1, DBD_MYSQL_STATEMENT);
  int named_columns = lua_toboolean (L, 2);

  return statement_fetch_impl (L, statement, named_columns);
} /* }}} */

/*
 * num_rows = statement:rowcount()
 */
static int
statement_rowcount (lua_State * L) /* {{{ */
{
  statement_t *statement =
    (statement_t *) luaL_checkudata (L, 1, DBD_MYSQL_STATEMENT);

  if (!statement->stmt)
    {
      luaL_error (L, DBI_ERR_INVALID_STATEMENT);
    }

  lua_pushinteger (L, mysql_stmt_num_rows (statement->stmt));

  return 1;
} /* }}} */

/*
 * iterfunc = statement:rows(named_indexes)
 */
static int
statement_rows (lua_State * L) /* {{{ */
{
  if (lua_gettop (L) == 1)
    {
      lua_pushvalue (L, 1);
      lua_pushboolean (L, 0);
    }
  else
    {
      lua_pushvalue (L, 1);
      lua_pushboolean (L, lua_toboolean (L, 2));
    }

  lua_pushcclosure (L, next_iterator, 2);
  return 1;
} /* }}} */

/*
 * __gc
 */
static int
statement_gc (lua_State * L) /* {{{ */
{
  /* always free the handle */
  statement_close (L);

  return 0;
} /* }}} */

/*
 * __tostring
 */
static int
statement_tostring (lua_State * L) /* {{{ */
{
  statement_t *statement =
    (statement_t *) luaL_checkudata (L, 1, DBD_MYSQL_STATEMENT);

  lua_pushfstring (L, "%s: %p", DBD_MYSQL_STATEMENT, statement);

  return 1;
} /* }}} */


int
dbd_mysql_statement_create (lua_State * L, connection_t * conn) /* {{{ */
{
  statement_t *statement = NULL;
  MYSQL_STMT *stmt = mysql_stmt_init (conn->mysql);

  if (!stmt)
    {
      lua_pushnil (L);
      lua_pushfstring (L, DBI_ERR_ALLOC_STATEMENT, mysql_error (conn->mysql));
      return 2;
    }

  statement = (statement_t *) lua_newuserdata (L, sizeof (statement_t));
  statement->mysql = conn->mysql;
  statement->stmt = stmt;
  statement->metadata = NULL;
  statement->bind = NULL;

  luaL_getmetatable (L, DBD_MYSQL_STATEMENT);
  lua_setmetatable (L, -2);

  return 1;
} /* }}} */

/*
 * success, err = stmt:prepare(query)
 */
static int
statement_prepare (lua_State * L) /* {{{ */
{
  statement_t *statement =
    (statement_t *) luaL_checkudata (L, 1, DBD_MYSQL_STATEMENT);
  const char *sql_query = luaL_checkstring (L, 2);
  unsigned long sql_len = strlen (sql_query);

  if (!statement->stmt)
    {
      luaL_error (L, DBI_ERR_INVALID_STATEMENT);
    }

  if (mysql_stmt_prepare (statement->stmt, sql_query, sql_len))
    {
      lua_pushnil (L);
      lua_pushfstring (L, DBI_ERR_PREP_STATEMENT,
		       mysql_stmt_error (statement->stmt));
      return 2;
    }

  lua_pushboolean (L, 1);
  return 1;
} /* }}} */

/*
 * success, event = stmt:prepare_start(query)
 */
static int
statement_prepare_start (lua_State * L) /* {{{ */
{
  statement_t *statement =
    (statement_t *) luaL_checkudata (L, 1, DBD_MYSQL_STATEMENT);
  const char *sql_query = luaL_checkstring (L, 2);
  unsigned long sql_len = strlen (sql_query);

  if (!statement->stmt)
    {
      luaL_error (L, DBI_ERR_INVALID_STATEMENT);
    }

  statement->event =
    mysql_stmt_prepare_start (&(statement->ret), statement->stmt,
			      sql_query, sql_len);

  if (statement->event & MYSQL_WAIT_EXCEPT)
    {
      lua_pushnil (L);
      lua_pushfstring (L, DBI_ERR_PREP_STATEMENT,
		       mysql_stmt_error (statement->stmt));
      return 2;
    }

  if (statement->event & MYSQL_WAIT_TIMEOUT)
    {
      statement->timeout =
	1000 * mysql_get_timeout_value_ms (statement->mysql);
    }

  lua_pushboolean (L, 1);
  lua_pushnumber (L, convert_mysql_to_ev (statement->event));
  return 2;
} /* }}} */

/*
 * success, err, event = stmt:prepare_cont(event)
 */
static int
statement_prepare_cont (lua_State * L) /* {{{ */
{
  statement_t *statement =
    (statement_t *) luaL_checkudata (L, 1, DBD_MYSQL_STATEMENT);
  int event = convert_ev_to_mysql (lua_tonumber (L, 2));

  if (!statement->stmt)
    {
      luaL_error (L, DBI_ERR_INVALID_STATEMENT);
    }
  statement->event =
    mysql_stmt_prepare_cont (&(statement->ret), statement->stmt, event);

  if (statement->event & MYSQL_WAIT_EXCEPT)
    {
      lua_pushnil (L);
      lua_pushfstring (L, DBI_ERR_PREP_STATEMENT,
		       mysql_stmt_error (statement->stmt));
    }

  if (statement->event & MYSQL_WAIT_TIMEOUT)
    {
      statement->timeout =
	1000 * mysql_get_timeout_value_ms (statement->mysql);
    }
  if (statement->ret)
    {
      lua_pushnil (L);
      lua_pushfstring (L, DBI_ERR_PREP_STATEMENT,
		       mysql_stmt_error (statement->stmt));
      return 2;
    }
  if (statement->event > 0)
    {
      lua_pushboolean (L, 0);
      lua_pushnumber (L, convert_mysql_to_ev (statement->event));
      return 2;
    }
  lua_pushboolean (L, 1);
  return 1;
} /* }}} */

static int
statement_get_event (lua_State * L) /* {{{ */
{
  statement_t *statement =
    (statement_t *) luaL_checkudata (L, 1, DBD_MYSQL_STATEMENT);
  lua_pushnumber (L, convert_mysql_to_ev (statement->event));
  return 1;
} /* }}} */

static int
statement_get_timeout (lua_State * L) /* {{{ */
{
  statement_t *statement =
    (statement_t *) luaL_checkudata (L, 1, DBD_MYSQL_STATEMENT);
  lua_pushnumber (L, statement->timeout);
  return 1;
} /* }}} */

static int
statement_get_socket (lua_State * L) /* {{{ */
{
  statement_t *statement =
    (statement_t *) luaL_checkudata (L, 1, DBD_MYSQL_STATEMENT);
  lua_pushnumber (L, mysql_get_socket (statement->mysql));
  return 1;
} /* }}} */


int
dbd_mysql_statement (lua_State * L)
{
  static const luaL_Reg statement_methods[] = {
    {"affected", statement_affected},
    {"close", statement_close},
    {"columns", statement_columns},
    {"execute", statement_execute},
    {"execute_start", statement_execute_start},
    {"execute_cont", statement_execute_cont},
    {"fetch", statement_fetch},
    {"fetch_start", statement_fetch_start},
    {"fetch_cont", statement_fetch_cont},
    {"get_event", statement_get_event},
    {"get_socket", statement_get_socket},
    {"get_timeout", statement_get_timeout},
    {"prepare", statement_prepare},
    {"prepare_start", statement_prepare_start},
    {"prepare_cont", statement_prepare_cont},
    {"rowcount", statement_rowcount},
    {"rows", statement_rows},
    {NULL, NULL}
  };

  static const luaL_Reg statement_class_methods[] = {
    {NULL, NULL}
  };

  luaL_newmetatable (L, DBD_MYSQL_STATEMENT);
  luaL_register (L, 0, statement_methods);
  lua_pushvalue (L, -1);
  lua_setfield (L, -2, "__index");

  lua_pushcfunction (L, statement_gc);
  lua_setfield (L, -2, "__gc");

  lua_pushcfunction (L, statement_tostring);
  lua_setfield (L, -2, "__tostring");

  luaL_register (L, DBD_MYSQL_STATEMENT, statement_class_methods);

  return 1;
}
