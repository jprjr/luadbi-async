#ifdef _MSC_VER  /* all MS compilers define this (version) */
	#include <windows.h> 
#endif


#include <mysql.h>
#include <event.h>
#include <dbd/common.h>

#define DBD_MYSQL_CONNECTION	"DBD.MySQL.Connection"
#define DBD_MYSQL_STATEMENT	"DBD.MySQL.Statement"

/*
 * connection object implementation
 */
typedef struct _connection {
    MYSQL *mysql;
    MYSQL *ret;
    int event;
    int timeout;
} connection_t;

/*
 * statement object implementation
 */
typedef struct _statement {
    MYSQL *mysql;
    MYSQL_STMT *stmt;
    int ret;
    int event;
    int timeout;
    MYSQL_RES *metadata; /* result dataset metadata */
} statement_t;

static int convert_ev_to_mysql (int status) {
    int ret = 0;
    if(status & EV_READ) {
        ret |= MYSQL_WAIT_READ;
    }
    if(status & EV_WRITE) {
        ret |= MYSQL_WAIT_WRITE;
    }
    if(status & EV_TIMEOUT) {
        ret |= MYSQL_WAIT_TIMEOUT;
    }
    return ret;
}
static int convert_mysql_to_ev (int status) {
    int ret = 0;
    if(status & MYSQL_WAIT_READ) {
        ret |= EV_READ;
    }
    if(status & MYSQL_WAIT_WRITE) {
        ret |= EV_WRITE;
    }
    if(status & MYSQL_WAIT_TIMEOUT) {
        ret |= EV_TIMEOUT;
    }
    return ret;
}
   
