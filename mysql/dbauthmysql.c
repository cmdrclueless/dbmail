/*
 * $Id$
 * (c) 2000-2002 IC&S, The Netherlands (http://www.ic-s.nl)
 * implementation of authentication functions &
 * user management for mySQL dbases
 */

#include "../auth.h"
#include "/usr/include/mysql/mysql.h"
#include "../list.h"
#include "../debug.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../db.h"
#include "../dbmd5.h"
#include <crypt.h>
#include "../config.h"

#define AUTH_QUERY_SIZE 1024

/* 
 * var's for dbase connection/query
 */
char __auth_query_data[AUTH_QUERY_SIZE]; 
MYSQL __auth_conn;
MYSQL_RES *__auth_res;
MYSQL_ROW __auth_row;

/*
 * auth_connect()
 *
 * initializes the connection for authentication.
 * 
 * returns 0 on success, -1 on failure.
 */
int auth_connect()
{
  mysql_init(&__auth_conn);
  if (mysql_real_connect (&__auth_conn,HOST,USER,PASS,MAILDATABASE,0,NULL,0) == NULL)
    {
      trace(TRACE_ERROR,"auth_connect(): mysql_real_connect failed: %s",
	    mysql_error(&__auth_conn));
      return -1;
    }

  return 0;
}


int auth_disconnect()
{
  mysql_close(&__auth_conn);
  return 0;
}


/* 
 * __auth_query()
 * 
 * Internal function for the authentication mechanism.
 * Executes the query q on the (global) connection __auth_conn.
 *
 * returns -1 on error, 0 on success.
 *
 */
int __auth_query(const char *q)
{
  if (!q)
    {
      trace(TRACE_ERROR, "__auth_query(): got NULL query");
      return -1;
    }

  /* ping the mySQL server
   * this function does a automatical re-connection attempt if
   * the connection is dead
   */
  if (mysql_ping(&__auth_conn) != 0)
    {
      trace(TRACE_ERROR, "__auth_query(): connection failure");
      return -1;
    }

  trace(TRACE_DEBUG,"__auth_query(): executing query [%s]", q);

  if (mysql_real_query(&__auth_conn, q, strlen(q)) < 0)
    {
      trace(TRACE_ERROR, "__auth_query(): query failed: [%s]",mysql_error(&__auth_conn));
      return -1;
    }
  
  return 0;
}


/* string to be returned by db_getencryption() */
#define _DESCSTRLEN 50
char __auth_encryption_desc_string[_DESCSTRLEN];


u64_t auth_user_exists(const char *username)
{
  u64_t uid;

  if (!username)
    {
      trace(TRACE_ERROR,"auth_user_exists(): got NULL as username\n");
      return 0;
    }

  snprintf(__auth_query_data, DEF_QUERYSIZE, "SELECT user_idnr FROM users WHERE userid='%s'",username);

  if (__auth_query(__auth_query_data)==-1)
    {
      trace(TRACE_ERROR, "auth_user_exists(): could not execute query\n");
      return -1;
    }

  if ((__auth_res = mysql_store_result(&__auth_conn)) == NULL) 
    {
      trace(TRACE_ERROR,"auth_user_exists: mysql_store_result failed: %s",mysql_error(&__auth_conn));
      return -1;
    }
  
  __auth_row = mysql_fetch_row(__auth_res);
  
  uid = (__auth_row && __auth_row[0]) ? strtoull(__auth_row[0], 0, 0) : 0;

  mysql_free_result(__auth_res);

  return uid;
}


/* return a list of existing users. -2 on mem error, -1 on db-error, 0 on succes */
int auth_get_known_users(struct list *users)
{
  if (!users)
    {
      trace(TRACE_ERROR,"auth_get_known_users(): got a NULL pointer as argument\n");
      return -2;
    }

  list_init(users);

  /* do a inverted (DESC) query because adding the names to the final list inverts again */
  snprintf(__auth_query_data, DEF_QUERYSIZE, "SELECT userid FROM users ORDER BY userid DESC");

  if (__auth_query(__auth_query_data) == -1)
    {
      trace(TRACE_ERROR,"auth_get_known_users(): could not retrieve user list\n");
      return -1;
    }

  if ((__auth_res = mysql_store_result(&__auth_conn)) == NULL) 
    {
      trace(TRACE_ERROR,"auth_get_known_users(): mysql_store_result failed: %s",mysql_error(&__auth_conn));
      return -1;
    }
  
  while ((__auth_row = mysql_fetch_row(__auth_res)))
    {
      if (!list_nodeadd(users, __auth_row[0], strlen(__auth_row[0])+1))
	{
	  list_freelist(&users->start);
	  return -2;
	}
    }
      
  mysql_free_result(__auth_res);
  return 0;
}


u64_t auth_getclientid(u64_t useridnr)
{
  u64_t cid;

  snprintf(__auth_query_data, DEF_QUERYSIZE, "SELECT client_idnr FROM users WHERE user_idnr = %llu",useridnr);

  if (__auth_query(__auth_query_data) == -1)
    {
      trace(TRACE_ERROR,"auth_getclientid(): could not retrieve client id for user [%llu]\n",useridnr);
      return -1;
    }

  if ((__auth_res = mysql_store_result(&__auth_conn)) == NULL)
    {
      trace(TRACE_ERROR,"auth_getclientid(): could not store query result: [%s]\n",mysql_error(&__auth_conn));
      return -1;
    }

  __auth_row = mysql_fetch_row(__auth_res);
  cid = (__auth_row && __auth_row[0]) ? strtoull(__auth_row[0], 0, 10) : 0;

  mysql_free_result(__auth_res);
  return cid;
}


u64_t auth_getmaxmailsize(u64_t useridnr)
{
  u64_t maxmailsize;

  snprintf(__auth_query_data, DEF_QUERYSIZE, "SELECT maxmail_size FROM users WHERE user_idnr = %llu",useridnr);

  if (__auth_query(__auth_query_data) == -1)
    {
      trace(TRACE_ERROR,"auth_getmaxmailsize(): could not retrieve client id for user [%llu]\n",useridnr);
      return -1;
    }

  if ((__auth_res = mysql_store_result(&__auth_conn)) == NULL)
    {
      trace(TRACE_ERROR,"auth_getmaxmailsize(): could not store query result: [%s]\n",
	    mysql_error(&__auth_conn));
      return -1;
    }

  __auth_row = mysql_fetch_row(__auth_res);
  maxmailsize = (__auth_row && __auth_row[0]) ? strtoull(__auth_row[0], 0, 10) : -1;

  mysql_free_result(__auth_res);
  return maxmailsize;
}


/*
 * auth_getencryption()
 *
 * returns a string describing the encryption used for the passwd storage
 * for this user.
 * The string is valid until the next function call; in absence of any 
 * encryption the string will be empty (not null).
 *
 * If the specified user does not exist an empty string will be returned.
 */
char* auth_getencryption(u64_t useridnr)
{
  __auth_encryption_desc_string[0] = 0;

  if (useridnr == -1 || useridnr == 0)
    {
      /* assume another function returned an error code (-1) 
       * or this user does not exist (0)
       */
      trace(TRACE_ERROR,"auth_getencryption(): got (%lld) as userid", useridnr);
      return __auth_encryption_desc_string; /* return empty */
    }
      
  snprintf(__auth_query_data, DEF_QUERYSIZE, "SELECT encryption_type FROM users WHERE user_idnr = %llu",
	   useridnr);

  if (__auth_query(__auth_query_data) == -1)
    {
      trace(TRACE_ERROR,"auth_getencryption(): could not retrieve encryption type for user [%llu]\n",
	    useridnr);
      return __auth_encryption_desc_string; /* return empty */
    }

  if ((__auth_res = mysql_store_result(&__auth_conn)) == NULL)
    {
      trace(TRACE_ERROR,"auth_getencryption(): could not store query result: [%s]\n",
	    mysql_error(&__auth_conn));
      return __auth_encryption_desc_string; /* return empty */
    }

  __auth_row = mysql_fetch_row(__auth_res);
  if (__auth_row && __auth_row[0])
    strncpy(__auth_encryption_desc_string, __auth_row[0], _DESCSTRLEN);

  mysql_free_result(__auth_res);
  return __auth_encryption_desc_string;
}


int auth_check_user (char *username, struct list *userids, int checks) 
{
  int occurences=0;
  MYSQL_RES *myres;
  MYSQL_ROW myrow;
  
  trace(TRACE_DEBUG,"auth_check_user(): checking user [%s] in alias table",username);
  
  snprintf (__auth_query_data, DEF_QUERYSIZE,  "SELECT deliver_to FROM aliases WHERE "
	    "alias=\"%s\"",username);

  trace(TRACE_DEBUG,"auth_check_user(): executing query, checks [%d]", checks);
  if (__auth_query(__auth_query_data)==-1)
      return 0;
  
  if ((myres = mysql_store_result(&__auth_conn)) == NULL) 
    {
      trace(TRACE_ERROR,"auth_check_user: mysql_store_result failed: [%s]",mysql_error(&__auth_conn));
      return 0;
    }

  if (mysql_num_rows(myres)<1) 
  {
    if (checks>0)
      {
	/* found the last one, this is the deliver to
	 * but checks needs to be bigger then 0 because
	 * else it could be the first query failure */

	list_nodeadd(userids, username, strlen(username)+1);
	trace (TRACE_DEBUG,"auth_check_user(): adding [%s] to deliver_to address",username);
	mysql_free_result(myres);
	return 1;
      }
    else
      {
	trace (TRACE_DEBUG,"auth_check_user(): user %s not in aliases table", username);
	mysql_free_result(myres);
	return 0; 
      }
  }
      
  trace (TRACE_DEBUG,"auth_check_user(): into checking loop");

  while ((myrow = mysql_fetch_row(myres))!=NULL)
  {
      /* do a recursive search for deliver_to */
      trace (TRACE_DEBUG,"auth_check_user(): checking user %s to %s",username, myrow[0]);
      occurences += auth_check_user (myrow[0], userids, 1);
  }
  
  /* trace(TRACE_INFO,"auth_check_user(): user [%s] has [%d] entries",username,occurences); */
  mysql_free_result(myres);

  return occurences;
}

	
/* 
 * auth_adduser()
 *
 * adds a new user to the database 
 * and adds a INBOX 
 * returns a useridnr on succes, -1 on failure 
 */
u64_t auth_adduser (char *username, char *password, char *enctype, char *clientid, char *maxmail)
{
  u64_t useridnr;
  char *tst;
  u64_t size;

  /* first check to see if this user already exists */
  snprintf(__auth_query_data, DEF_QUERYSIZE, "SELECT * FROM users WHERE userid = '%s'", username);

  if (__auth_query(__auth_query_data) == -1)
    {
      /* query failed */
      trace (TRACE_ERROR, "auth_adduser(): query failed\n");
      return -1;
    }

  if ((__auth_res = mysql_store_result(&__auth_conn)) == NULL) 
    {
      trace(TRACE_ERROR,"auth_adduser(): mysql_store_result failed: %s",mysql_error(&__auth_conn));
      return 0;
    }

  if (mysql_num_rows(__auth_res) > 0)
    {
      /* this username already exists */
      trace(TRACE_ERROR,"auth_adduser(): user already exists\n");
      return -1;
    }

  mysql_free_result(__auth_res);

  size = strtoull(maxmail,&tst,10);
  if (tst)
    {
      if (tst[0] == 'M' || tst[0] == 'm')
	size *= 1000000;

      if (tst[0] == 'K' || tst[0] == 'k')
	size *= 1000;
    }
      
  snprintf (__auth_query_data, DEF_QUERYSIZE,"INSERT INTO users "
	    "(userid,passwd,client_idnr,maxmail_size,encryption_type) VALUES "
	    "('%s','%s',%s,%llu,'%s')",
	    username,password,clientid, size, enctype ? enctype : "");
	
  if (__auth_query(__auth_query_data) == -1)
    {
      /* query failed */
      trace (TRACE_ERROR, "auth_adduser(): query for adding user failed");
      return -1;
    }

  useridnr = mysql_insert_id(&__auth_conn);
	
  /* creating query for adding mailbox */
  snprintf (__auth_query_data, DEF_QUERYSIZE,"INSERT INTO mailboxes (owner_idnr, name) VALUES (%llu,'INBOX')",
	   useridnr);
	
  trace (TRACE_DEBUG,"auth_adduser(): executing query for mailbox");

	
  if (__auth_query(__auth_query_data))
    {
      trace (TRACE_ERROR,"auth_adduser(): query failed for adding mailbox");
      return -1;
    }

  
  return useridnr;
}


int auth_delete_user(const char *username)
{
  snprintf (__auth_query_data, DEF_QUERYSIZE, "DELETE FROM users WHERE userid = '%s'",username);

  if (__auth_query(__auth_query_data) == -1)
    {
      /* query failed */
      trace (TRACE_ERROR, "auth_delete_user(): query for removing user failed");
      return -1;
    }

  return 0;
}
  
int auth_change_username(u64_t useridnr, const char *newname)
{
  snprintf(__auth_query_data, DEF_QUERYSIZE, "UPDATE users SET userid = '%s' WHERE user_idnr=%llu", 
	   newname, useridnr);

  if (__auth_query(__auth_query_data) == -1)
    {
      trace(TRACE_ERROR,"auth_change_username(): could not change name for user [%llu]\n",useridnr);
      return -1;
    }

  return 0;
}


int auth_change_password(u64_t useridnr, const char *newpass, const char *enctype)
{
  snprintf(__auth_query_data, DEF_QUERYSIZE, "UPDATE users SET passwd = '%s', encryption_type = '%s' "
	   "WHERE user_idnr=%llu", 
	   newpass, enctype ? enctype : "", useridnr);

  if (__auth_query(__auth_query_data) == -1)
    {
      trace(TRACE_ERROR,"auth_change_password(): could not change passwd for user [%llu]\n",useridnr);
      return -1;
    }

  return 0;
}


int auth_change_clientid(u64_t useridnr, u64_t newcid)
{
  snprintf(__auth_query_data, DEF_QUERYSIZE, "UPDATE users SET client_idnr = %llu WHERE user_idnr=%llu", 
	   newcid, useridnr);

  if (__auth_query(__auth_query_data) == -1)
    {
      trace(TRACE_ERROR,"auth_change_password(): could not change client id for user [%llu]\n",useridnr);
      return -1;
    }

  return 0;
}

int auth_change_mailboxsize(u64_t useridnr, u64_t newsize)
{
  snprintf(__auth_query_data, DEF_QUERYSIZE, "UPDATE users SET maxmail_size = %llu WHERE user_idnr=%llu", 
	   newsize, useridnr);

  if (__auth_query(__auth_query_data) == -1)
    {
      trace(TRACE_ERROR,"auth_change_password(): could not change maxmailsize for user [%llu]\n",
	    useridnr);
      return -1;
    }

  return 0;
}


/* 
 * auth_validate()
 *
 * tries to validate user 'user'
 *
 * returns useridnr on OK, 0 on validation failed, -1 on error 
 */
u64_t auth_validate (char *user, char *password)
{
  u64_t id;
  int is_validated = 0;
  
  snprintf(__auth_query_data, DEF_QUERYSIZE, "SELECT user_idnr, passwd, encryption_type FROM users "
	   "WHERE userid = '%s'", user);

  if (__auth_query(__auth_query_data)==-1)
    {
      trace(TRACE_ERROR, "auth_validate(): could not select user information");
      return -1;
    }
	
  if ((__auth_res = mysql_store_result(&__auth_conn)) == NULL)
    {
      trace (TRACE_ERROR,"auth_validate(): mysql_store_result failed: %s\n",mysql_error(&__auth_conn));
      return -1;
    }

  __auth_row = mysql_fetch_row(__auth_res);
  
  if (!__auth_row)
    {
      /* user does not exist */
      mysql_free_result(__auth_res);
      return 0;
    }

  if (!__auth_row[2] || strcasecmp(__auth_row[2], "") == 0)
    {
      trace (TRACE_DEBUG,"auth_validate(): validating using cleartext passwords");
      is_validated = (strcmp(__auth_row[1], password) == 0) ? 1 : 0;
    }
  else if ( strcasecmp(__auth_row[2], "crypt") == 0)
    {
      trace (TRACE_DEBUG,"auth_validate(): validating using crypt() encryption");
      is_validated = (strcmp( crypt(password, __auth_row[1]), __auth_row[1]) == 0) ? 1 : 0;
    }
  
  if (is_validated)
    id = (__auth_row[0]) ? strtoull(__auth_row[0], NULL, 10) : 0;
  else
    id = 0;

  mysql_free_result(__auth_res);

  return id;
}

u64_t auth_md5_validate (char *username,unsigned char *md5_apop_he, char *apop_stamp)
{
  /* returns useridnr on OK, 0 on validation failed, -1 on error */
  
  char *checkstring;
  unsigned char *md5_apop_we;
  u64_t useridnr;	

  
  
  snprintf (__auth_query_data, DEF_QUERYSIZE, "SELECT passwd,user_idnr FROM users WHERE userid=\"%s\"",username);
	
  if (__auth_query(__auth_query_data)==-1)
    {
      
      return -1;
    }
	
  if ((__auth_res = mysql_store_result(&__auth_conn)) == NULL)
    {
      trace (TRACE_ERROR,"auth_md5_validate(): mysql_store_result failed:  %s",mysql_error(&__auth_conn));
      
      return -1;
    }

  if (mysql_num_rows(__auth_res)<1)
    {
      /* no such user found */
      
      return 0;
    }
	
  __auth_row = mysql_fetch_row(__auth_res);
	
	/* now authenticate using MD5 hash comparisation 
	 * __auth_row[0] contains the password */

  trace (TRACE_DEBUG,"auth_md5_validate(): apop_stamp=[%s], userpw=[%s]",apop_stamp,__auth_row[0]);
	
  memtst((checkstring=(char *)my_malloc(strlen(apop_stamp)+strlen(__auth_row[0])+2))==NULL);
  snprintf(checkstring, strlen(apop_stamp)+strlen(__auth_row[0])+2, "%s%s",apop_stamp,__auth_row[0]);

  md5_apop_we=makemd5(checkstring);
	
  trace(TRACE_DEBUG,"auth_md5_validate(): checkstring for md5 [%s] -> result [%s]",checkstring,
	md5_apop_we);
  trace(TRACE_DEBUG,"auth_md5_validate(): validating md5_apop_we=[%s] md5_apop_he=[%s]",
	md5_apop_we, md5_apop_he);

  if (strcmp(md5_apop_he,makemd5(checkstring))==0)
    {
      trace(TRACE_MESSAGE,"auth_md5_validate(): user [%s] is validated using APOP",username);
		
      useridnr = (__auth_row && __auth_row[1]) ? strtoull(__auth_row[1], NULL, 10) : 0;
	
      mysql_free_result(__auth_res);
      
      my_free(checkstring);

      return useridnr;
    }
	
  trace(TRACE_MESSAGE,"auth_md5_validate(): user [%s] could not be validated",username);

  if (__auth_res!=NULL)
    mysql_free_result(__auth_res);
  
  
  my_free(checkstring);
  
  return 0;
}


char *auth_get_userid (u64_t *useridnr)
{
  /* returns the mailbox id (of mailbox inbox) for a user or a 0 if no mailboxes were found */
  
  char *returnid = NULL;
  
  snprintf (__auth_query_data, DEF_QUERYSIZE,"SELECT userid FROM users WHERE user_idnr = %llu",
	   *useridnr);

  trace(TRACE_DEBUG,"auth_get_userid(): executing query");
  if (__auth_query(__auth_query_data)==-1)
    {
      return 0;
    }

  if ((__auth_res = mysql_store_result(&__auth_conn)) == NULL) 
    {
      trace(TRACE_ERROR,"auth_get_userid(): mysql_store_result failed: %s",mysql_error(&__auth_conn));
      
      return 0;
    }

  if (mysql_num_rows(__auth_res)<1) 
    {
      trace (TRACE_DEBUG,"auth_get_userid(): user has no username?");
      mysql_free_result(__auth_res);
      
      return 0; 
    } 

  if ((__auth_row = mysql_fetch_row(__auth_res))==NULL)
    {
      trace (TRACE_DEBUG,"auth_get_userid(): fetch_row call failed");
      mysql_free_result(__auth_res);
      return NULL;
    }

  if (__auth_row[0])
    {
      if (!(returnid = (char *)my_malloc(strlen(__auth_row[0])+1)))
	{
	  trace(TRACE_ERROR,"auth_get_userid(): out of memory");
	  mysql_free_result(__auth_res);
	  return NULL;
	}
	  
      strcpy (returnid, __auth_row[0]);
    }
  
  mysql_free_result(__auth_res);
  
  return returnid;
}
