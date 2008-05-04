/*
 *   Copyright (c) 2005-2006 NFG Net Facilities Group BV support@nfg.nl
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation; either
 *   version 2 of the License, or (at your option) any later
 *   version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *   
 *
 *
 *  
 *
 *   Basic unit-test framework for dbmail (www.dbmail.org)
 *
 *   See http://check.sf.net for details and docs.
 *
 *
 *   Run 'make check' to see some action.
 *
 */ 

#include <check.h>
#include "check_dbmail.h"

extern char *configFile;
extern db_param_t _db_params;

#define DBPFX _db_params.pfx

/* we need this one because we can't directly link imapd.o */
extern u64_t msgbuf_idx;
extern u64_t msgbuf_buflen;

extern char *multipart_message;
extern char *multipart_message_part;
extern char *raw_lmtp_data;

/*
 *
 * the test fixtures
 *
 */
	
void init_testuser1(void) 
{
        u64_t user_idnr;
	if (! (auth_user_exists("testuser1",&user_idnr)))
		auth_adduser("testuser1","test", "md5", 101, 1024000, &user_idnr);
}
	
void setup(void)
{
	configure_debug(5,0);
	config_read(configFile);
	GetDBParams();
	db_connect();
	auth_connect();
	init_testuser1();
}

void teardown(void)
{
	auth_disconnect();
	db_disconnect();
	config_free();
}

START_TEST(test_dbmail_imap_plist_as_string)
{
	char *result;
	GList *l;

	l = NULL;
	l = g_list_append(l, "NIL");
	l = g_list_append(l, "NIL");
	result = dbmail_imap_plist_as_string(l);
	fail_unless(strcmp(result,"(NIL NIL)")==0,"plist construction failed");
	
	//g_list_foreach(l,(GFunc)g_free,NULL);
	g_free(result);
	
	l = NULL;
	l = g_list_append(l, "(NIL NIL)");
	result = dbmail_imap_plist_as_string(l);
	fail_unless(strcmp(result,"(NIL NIL)")==0,"plist construction failed");
	
	//g_list_foreach(l,(GFunc)g_free,NULL);
	g_free(result);

	l = g_list_append(NULL, "NIL");
	l = g_list_append(l, "NIL");
	l = g_list_append(l, "(NIL NIL)");
	l = g_list_append(l, "(NIL NIL)");
	result = dbmail_imap_plist_as_string(l);
	fail_unless(strcmp(result,"(NIL NIL (NIL NIL) (NIL NIL))")==0,"plist construction failed");
	
	//g_list_foreach(l,(GFunc)g_free,NULL);
	g_free(result);
}
END_TEST

START_TEST(test_dbmail_imap_plist_collapse)
{
	char *result;
	char *in = "(NIL) (NIL) (NIL)";
	result = dbmail_imap_plist_collapse(in);
	fail_unless(strcmp(result,"(NIL)(NIL)(NIL)")==0,"plist collapse failed");
	g_free(result);
}
END_TEST


#define A(x,y) s=dbmail_imap_astring_as_string(x); \
	fail_unless(strcmp(y,s)==0,"dbmail_imap_astring_as_string failed [%s] != [%s]", s, y); \
	g_free(s)
START_TEST(test_dbmail_imap_astring_as_string)
{
	char *s;
	A("test","\"test\"");
	A("\"test\"","\"test\"");
	A("\"test\" \"test\"","{13}\r\n\"test\" \"test\"");
	A("\"test","{5}\r\n\"test");
	A("test�","{5}\r\ntest�");
	A("test\"","{5}\r\ntest\"");
	A("test\"","{5}\r\ntest\"");
	A("test\\","{5}\r\ntest\\");
	A("test\225","{5}\r\ntest\225");
}
END_TEST

static clientinfo_t * ci_new(void)
{
	clientinfo_t *ci = g_new0(clientinfo_t,1);
	FILE *fd = fopen("/dev/null","w");
	ci->rx = fileno(stdin);
	ci->tx = fileno(fd);
	return ci;
}

static char *tempfile;
static clientinfo_t * ci_new_writable(void)
{
	clientinfo_t *ci = ci_new();

	tempfile = tmpnam(NULL);
	mkfifo(tempfile, 0600);

	// Open r+ because we're controlling both sides.
	ci->rx = fileno(fopen(tempfile, "r+"));
	ci->tx = fileno(fopen(tempfile, "r+"));
	return ci;
}

static void ci_free_writable(clientinfo_t *ci)
{
	close(ci->tx);
	close(ci->rx);
	unlink(tempfile);
}

//ImapSession * dbmail_imap_session_new(void);
START_TEST(test_imap_session_new)
{
	ImapSession *s;
	s = dbmail_imap_session_new();
	fail_unless(s!=NULL, "Failed to initialize imapsession");
	dbmail_imap_session_delete(s);
}
END_TEST
//ImapSession * dbmail_imap_session_setTag(ImapSession * self, char * tag);
//ImapSession * dbmail_imap_session_setCommand(ImapSession * self, char * command);
//ImapSession * dbmail_imap_session_setArgs(ImapSession * self, char ** args);
//ImapSession * dbmail_imap_session_setMsginfo(ImapSession * self, MessageInfo * msginfo);
//ImapSession * dbmail_imap_session_resetFi(ImapSession * self);
//void dbmail_imap_session_delete(ImapSession * self);
//int dbmail_imap_session_readln(ImapSession * self, char * buffer);
//int dbmail_imap_session_discard_to_eol(ImapSession *self);
//int dbmail_imap_session_printf(ImapSession * self, char * message, ...);
//int dbmail_imap_session_set_state(ImapSession *self, int state);
//int check_state_and_args(ImapSession * self, const char * command, int minargs, int maxargs, int state);
//int dbmail_imap_session_handle_auth(ImapSession * self, char * username, char * password);
//int dbmail_imap_session_prompt(ImapSession * self, char * prompt, char * value);
//u64_t dbmail_imap_session_mailbox_get_idnr(ImapSession * self, char * mailbox);
//int dbmail_imap_session_mailbox_check_acl(ImapSession * self, u64_t idnr, ACLRight_t right);
//int dbmail_imap_session_mailbox_get_selectable(ImapSession * self, u64_t idnr);
//int dbmail_imap_session_mailbox_show_info(ImapSession * self);
//int dbmail_imap_session_mailbox_open(ImapSession * self, char * mailbox);


//dbmail_imap_session_handle_auth(self, const char *username, const char *password);

START_TEST(test_imap_bodyfetch)
{
	int result;
	guint64 octet;
	ImapSession *s = dbmail_imap_session_new();

	dbmail_imap_session_bodyfetch_new(s);
	
	fail_unless(0 == dbmail_imap_session_bodyfetch_get_last_octetstart(s), "octetstart init value incorrect");
	fail_unless(0 == dbmail_imap_session_bodyfetch_get_last_octetcnt(s), "octetcnt init value incorrect");
	fail_unless(0 == dbmail_imap_session_bodyfetch_get_last_argstart(s), "argstart init value incorrect");
	
	s->args_idx = 23;
	dbmail_imap_session_bodyfetch_set_argstart(s);
	result = dbmail_imap_session_bodyfetch_get_last_argstart(s);
	fail_unless(result==23, "argstart incorrect");
	
	dbmail_imap_session_bodyfetch_set_octetstart(s,0);
	octet = dbmail_imap_session_bodyfetch_get_last_octetstart(s);
	fail_unless(octet==0, "octetstart incorrect");
	
	dbmail_imap_session_bodyfetch_set_octetcnt(s,12288);
	octet = dbmail_imap_session_bodyfetch_get_last_octetcnt(s);
	fail_unless(octet==12288, "octetcnt incorrect");
	
	dbmail_imap_session_delete(s);
		
}
END_TEST
START_TEST(test_g_mime_object_get_body)
{
	char * result;
	DbmailMessage *m;

	m = dbmail_message_new();
	m = dbmail_message_init_with_string(m,g_string_new(multipart_message));
	
	result = g_mime_object_get_body(GMIME_OBJECT(m->content));
	fail_unless(strlen(result)==1045,"g_mime_object_get_body failed");
	g_free(result);
	dbmail_message_free(m);
	
}
END_TEST

START_TEST(test_imap_get_structure)
{
	DbmailMessage *message;
	char *result;
	char *expect = g_new0(char,1024);


	/* bare bones */
	message = dbmail_message_new();
	message = dbmail_message_init_with_string(message, g_string_new(simple));
	result = imap_get_structure(GMIME_MESSAGE(message->content), 1);
	dbmail_message_free(message);
	g_free(result);

	/* multipart */
	message = dbmail_message_new();
	message = dbmail_message_init_with_string(message, g_string_new(multipart_message));
	result = imap_get_structure(GMIME_MESSAGE(message->content), 1);
	strncpy(expect,"((\"text\" \"html\" NIL NIL NIL \"7BIT\" 16 1 NIL (\"inline\" NIL) NIL NIL)"
			"(\"text\" \"plain\" (\"charset\" \"us-ascii\" \"name\" \"testfile\") NIL NIL \"base64\" 432 7 NIL NIL NIL NIL)"
			" \"mixed\" (\"boundary\" \"boundary\") NIL NIL NIL)",1024);
	fail_unless(strncasecmp(result,expect,1024)==0, "imap_get_structure failed");
	g_free(result);
	dbmail_message_free(message);

	/* multipart alternative */
	message = dbmail_message_new();
	message = dbmail_message_init_with_string(message, g_string_new(multipart_alternative));
	result = imap_get_structure(GMIME_MESSAGE(message->content), 1);
	strncpy(expect,"(((\"TEXT\" \"PLAIN\" (\"CHARSET\" \"ISO-8859-1\") NIL NIL \"7BIT\" 281 10 NIL NIL NIL NIL)(\"TEXT\" \"HTML\" (\"CHARSET\" \"ISO-8859-1\") NIL NIL \"7BIT\" 759 17 NIL NIL NIL NIL) \"ALTERNATIVE\" (\"BOUNDARY\" \"------------040302030903000400040101\") NIL NIL NIL)(\"IMAGE\" \"JPEG\" (\"NAME\" \"jesse_2.jpg\") NIL NIL \"BASE64\" 262 NIL (\"INLINE\" (\"FILENAME\" \"jesse_2.jpg\")) NIL NIL) \"MIXED\" (\"BOUNDARY\" \"------------050000030206040804030909\") NIL NIL NIL)",1024);

	fail_unless(strncasecmp(result,expect,1024)==0, "imap_get_structure failed");
	g_free(result);
	dbmail_message_free(message);
	
	/* text/plain */
	message = dbmail_message_new();
	message = dbmail_message_init_with_string(message, g_string_new(rfc822));
	result = imap_get_structure(GMIME_MESSAGE(message->content), 1);
	strncpy(expect,"(\"text\" \"plain\" (\"charset\" \"us-ascii\") NIL NIL \"7bit\" 32 4 NIL NIL NIL NIL)",1024);
	fail_unless(strncasecmp(result,expect,1024)==0, "imap_get_structure failed");
	g_free(result);
	g_free(expect);
	dbmail_message_free(message);

}
END_TEST

START_TEST(test_internet_address_parse_string)
{
	char * trythese [] = { "undisclosed-recipients", "undisclosed-recipients;",
		"undisclosed-recipients:", "undisclosed-recipients:;",
		"undisclosed-recipients: ;", NULL };
	int i;

	for (i = 0; trythese[i] != NULL; i++) {

		char *result = trythese[i];

		char *t;
		InternetAddressList *alist;
		char *expect = "((NIL NIL \"undisclosed-recipients\" NIL))";
		GList *list = NULL;

		t = imap_cleanup_address(result);
		alist = internet_address_parse_string(t);
		g_free(t);
		list = dbmail_imap_append_alist_as_plist(list, (const InternetAddressList *)alist);
		internet_address_list_destroy(alist);
		alist = NULL;

		result = dbmail_imap_plist_as_string(list);

		fail_unless(strcmp(result,expect)==0, "internet_address_parse_string failed to generate correct undisclosed-recipients plist, expected [%s] got [%s]", expect, result);

		g_list_destroy(list);
		g_free(result);

	}

	char * testlist[][2] = {
		{ "<i_am_not@broken.org>", "((NIL NIL \"i_am_not\" \"broken.org\"))" },
		{ "Break me: <foo@bar.org>", "((NIL NIL \"Break me\" NIL)(NIL NIL \"foo\" \"bar.org\"))" },
		{ "Joe's Friends: mary@joe.com, joe@joe.com, jane@joe.com;",
			"((NIL NIL \"Joe's Friends\" NIL)(NIL NIL \"mary\" \"joe.com\")"
			"(NIL NIL \"joe\" \"joe.com\")(NIL NIL \"jane\" \"joe.com\"))" },
		// These have the wrong separator; ms lookout style.
		{ "one@my.dom;two@my.dom", "((NIL NIL \"one\" \"my.dom\")(NIL NIL \"two\" \"my.dom\"))" },
		{ "one@my.dom; two@my.dom", "((NIL NIL \"one\" \"my.dom\")(NIL NIL \"two\" \"my.dom\"))" },
		{ "Group: one@my.dom;, two@my.dom", "((NIL NIL \"Group\" NIL)(NIL NIL \"one\" \"my.dom\")(NIL NIL \"two\" \"my.dom\"))" },
		{ NULL, NULL }
	};

	char *input, *expect;

	for (i = 0; testlist[i][0] != NULL; i++) {
		input = testlist[i][0];
		expect = testlist[i][1];

		InternetAddressList *alist;
		GList *list = NULL;
		char *result;
		int res;
		char *t;
		t = imap_cleanup_address(input);
		alist = internet_address_parse_string(t);
		list = dbmail_imap_append_alist_as_plist(list, (const InternetAddressList *)alist);
		result = dbmail_imap_plist_as_string(list);
		res = strcmp(result, expect);

		fail_unless(res == 0, "dbmail_imap_append_alist_as_plist failed, expected:\n[%s]\ngot:\n[%s]\n", expect, result);

		internet_address_list_destroy(alist);
		alist = NULL;
		g_list_destroy(list);
		g_free(result);
		g_free(t);
	}
}
END_TEST

START_TEST(test_imap_get_envelope)
{
	DbmailMessage *message;
	char *result, *expect;
	GString *s;
	
	expect = g_new0(char, 1024);
	
	/* text/plain */
	message = dbmail_message_new();
	s = g_string_new(rfc822);
	message = dbmail_message_init_with_string(message, s);
	g_string_free(s,TRUE);
	result = imap_get_envelope(GMIME_MESSAGE(message->content));
	strncpy(expect,"(\"Thu, 01 Jan 1970 00:00:00 +0000\" \"dbmail test message\" ((NIL NIL \"somewher\" \"foo.org\")) ((NIL NIL \"somewher\" \"foo.org\")) ((NIL NIL \"somewher\" \"foo.org\")) ((NIL NIL \"testuser\" \"foo.org\")) NIL NIL NIL \"<1199706209l.3020l.1l@(none)>\")",1024);
	fail_unless(strncasecmp(result,expect,1024)==0, "imap_get_envelope failed\n[%s] !=\n[%s]\n", result,expect);

	dbmail_message_free(message);
	g_free(result);
	result = NULL;

	/* bare bones message */
	message = dbmail_message_new();
	s = g_string_new(simple);
	message = dbmail_message_init_with_string(message, s);
	g_string_free(s,TRUE);
	result = imap_get_envelope(GMIME_MESSAGE(message->content));

	strncpy(expect,"(\"Thu, 01 Jan 1970 00:00:00 +0000\" \"dbmail test message\" NIL NIL NIL NIL NIL NIL NIL \"<1199706209l.3020l.0l@(none)>\")", 1024);
	fail_unless(strncasecmp(result,expect,1024)==0, "imap_get_envelope failed\n[%s] !=\n[%s]\n", result,expect);

	dbmail_message_free(message);
	g_free(result);
	result = NULL;

	g_free(expect);
	expect = NULL;

}
END_TEST

START_TEST(test_imap_get_envelope_8bit_id)
{
	DbmailMessage *message;
	char *result, *expect;
	
	const char *msgid = "<000001c1f64e$c4a34180$0100007f@z=F0=B5=D241>";
	expect = g_new0(char, 1024);
	
	/* text/plain */
	message = dbmail_message_new();
	message = dbmail_message_init_with_string(message, g_string_new(rfc822));
	dbmail_message_set_header(message,"Message-ID",msgid);
	
	result = imap_get_envelope(GMIME_MESSAGE(message->content));
	strncpy(expect,"(\"Thu, 01 Jan 1970 00:00:00 +0000\" \"dbmail test message\" ((NIL NIL \"somewher\" \"foo.org\")) ((NIL NIL \"somewher\" \"foo.org\")) ((NIL NIL \"somewher\" \"foo.org\")) ((NIL NIL \"testuser\" \"foo.org\")) NIL NIL NIL NIL)",1024);
	fail_unless(strncasecmp(result,expect,1024)==0, "imap_get_envelope failed");
	g_free(result);
	
	dbmail_message_set_header(message,"Message-ID","<123123123@foo.bar>");
	result = imap_get_envelope(GMIME_MESSAGE(message->content));
	strncpy(expect,"(\"Thu, 01 Jan 1970 00:00:00 +0000\" \"dbmail test message\" ((NIL NIL \"somewher\" \"foo.org\")) ((NIL NIL \"somewher\" \"foo.org\")) ((NIL NIL \"somewher\" \"foo.org\")) ((NIL NIL \"testuser\" \"foo.org\")) NIL NIL NIL \"<123123123@foo.bar>\")",1024);
	fail_unless(strncasecmp(result,expect,1024)==0, "imap_get_envelope failed");

	dbmail_message_free(message);
	g_free(result);
	g_free(expect);
}
END_TEST

START_TEST(test_imap_get_envelope_koi)
{
	char *t;
	const char *exp = "(\"Thu, 01 Jan 1970 00:00:00 +0000\" \"test\" ((\"=?koi8-r?Q?=E1=CE=D4=CF=CE=20=EE=C5=C8=CF=D2=CF=DB=C9=C8=20?=\" NIL \"bad\" \"foo.ru\")) ((\"=?koi8-r?Q?=E1=CE=D4=CF=CE=20=EE=C5=C8=CF=D2=CF=DB=C9=C8=20?=\" NIL \"bad\" \"foo.ru\")) ((\"=?koi8-r?Q?=E1=CE=D4=CF=CE=20=EE=C5=C8=CF=D2=CF=DB=C9=C8=20?=\" NIL \"bad\" \"foo.ru\")) ((NIL NIL \"nobody\" \"foo.ru\")) NIL NIL NIL \"<1199706209l.3020l.5l@(none)>\")";

	DbmailMessage *m = dbmail_message_new();
	GString *s = g_string_new(encoded_message_koi);

	m = dbmail_message_init_with_string(m, s);
	g_string_free(s,TRUE);
	
	t = imap_get_envelope(GMIME_MESSAGE(m->content));
 	fail_unless(strcmp(t,exp)==0,"encode/decode/encode loop failed\n[%s] !=\n[%s]", t,exp);

	g_free(t);
	dbmail_message_free(m);
	
}
END_TEST


			
#define F(a,b) fail_unless(strcmp(c = imap_cleanup_address(a), b)==0, "\n[%s] should have yielded \n[%s] but got \n[%s]", a,b,c)
#define Fnull(a,b) fail_unless(strcmp(c = imap_cleanup_address(a), b)==0, "\n[] should have yielded \n[" b "] but got \n[%s]", c)
	
START_TEST(test_imap_cleanup_address)
{
	char *c;

	F("=?iso-8859-1?Q?B=BA_V._F._Z=EAzere?= <nobody@nowhere.org>","\"=?iso-8859-1?Q?B=BA_V._F._Z=EAzere?=\" <nobody@nowhere.org>");
	F("=?iso-8859-1?Q?\"B=BA_V._F._Z=EAzere\"?=<nobody@nowhere.org>","\"=?iso-8859-1?Q?B=BA_V._F._Z=EAzere?=\" <nobody@nowhere.org>");
	F("=?iso-8859-1?Q?B=BA_V._F._Z=EAzere?=<nobody@nowhere.org>","\"=?iso-8859-1?Q?B=BA_V._F._Z=EAzere?=\" <nobody@nowhere.org>");
	F("\"=?iso-8859-1?Q?B=BA_V._F._Z=EAzere?=\" <nobody@nowhere.org>","\"=?iso-8859-1?Q?B=BA_V._F._Z=EAzere?=\" <nobody@nowhere.org>");
	F("", "");
	Fnull(NULL, "");
	F("Some One <some@foo.org>", "Some One <some@foo.org>");
	F(" <some@foo.org>", "<some@foo.org>");
	F("=?ISO-8859-2?Q? \"Verlag=20Dash=F6fer=20-=20DU.cz?= =?ISO-8859-2?Q?\" ?= <e-noviny@smtp.dashofer.cz>",
	"\"=?ISO-8859-2?Q?Verlag=20Dash=F6fer=20-=20DU.cz?= =?ISO-8859-2?Q?"
		/* Stringify here to kill the '??=' trigraph. */ "?=\" <e-noviny@smtp.dashofer.cz>");
	F("=?ISO-8859-2?Q?=22Miroslav_=A9ulc_=28fordfrog=29=22?=\n"
	"	<fordfrog@gentoo.org>\n", "\"=?ISO-8859-2?Q?=22Miroslav_=A9ulc_=28fordfrog=29=22?=\" <fordfrog@gentoo.org>");

	F("=?iso-8859-1?Q?::_=5B_Arrty_=5D_::_=5B_Roy_=28L=29_St=E8phanie_=5D?=  <over.there@hotmail.com>",
		"\"=?iso-8859-1?Q?::_=5B_Arrty_=5D_::_=5B_Roy_=28L=29_St=E8phanie_=5D?=\" <over.there@hotmail.com>");

	F("\"First Address\" <first@foo.com>, =?iso-8859-1?Q?::_=5B_Arrty_=5D_::_=5B_Roy_=28L=29_St=E8phanie_=5D?=  <over.there@hotmail.com>",
		"\"First Address\" <first@foo.com>, \"=?iso-8859-1?Q?::_=5B_Arrty_=5D_::_=5B_Roy_=28L=29_St=E8phanie_=5D?=\" <over.there@hotmail.com>");


}
END_TEST

START_TEST(test_imap_get_envelope_latin)
{
	char *t;
	char *expect = g_new0(char,1024);
	DbmailMessage *m;
	GString *s;
	

	/*  */
	m = dbmail_message_new();
	s = g_string_new(encoded_message_latin_1);
	m = dbmail_message_init_with_string(m, s);
	g_string_free(s,TRUE);
	
	t = imap_get_envelope(GMIME_MESSAGE(m->content));
	
	strncpy(expect,"(\"Thu, 01 Jan 1970 00:00:00 +0000\" \"=?iso-8859-1?Q?Re:_M=F3dulo_Extintores?=\" ((\"=?iso-8859-1?Q?B=BA_V._F._Z=EAzere?=\" NIL \"nobody\" \"nowhere.org\")) ((\"=?iso-8859-1?Q?B=BA_V._F._Z=EAzere?=\" NIL \"nobody\" \"nowhere.org\")) ((\"=?iso-8859-1?Q?B=BA_V._F._Z=EAzere?=\" NIL \"nobody\" \"nowhere.org\")) ((NIL NIL \"nobody\" \"foo.org\")) NIL NIL NIL \"<1199706209l.3020l.7l@(none)>\")",1024);
	
	fail_unless(strcmp(t,expect)==0,"imap_get_envelope failed\n%s\n%s\n ", expect, t);

	g_free(t);
	dbmail_message_free(m);

	/*  */
	m = dbmail_message_new();
	s = g_string_new(encoded_message_latin_2);
	m = dbmail_message_init_with_string(m, s);
	g_string_free(s,TRUE);
	
 	strncpy(expect,"(\"Thu, 01 Jan 1970 00:00:00 +0000\" \"=?ISO-8859-2?Q?Re=3A_=5Bgentoo-dev=5D_New_developer=3A__?= =?ISO-8859-2?Q?Miroslav_=A9ulc_=28fordfrog=29?=\" ((\"=?ISO-8859-2?Q?=22Miroslav_=A9ulc_=28fordfrog=29=22?=\" NIL \"fordfrog\" \"gentoo.org\")) ((\"=?ISO-8859-2?Q?=22Miroslav_=A9ulc_=28fordfrog=29=22?=\" NIL \"fordfrog\" \"gentoo.org\")) ((\"=?ISO-8859-2?Q?=22Miroslav_=A9ulc_=28fordfrog=29=22?=\" NIL \"fordfrog\" \"gentoo.org\")) ((NIL NIL \"gentoo-dev\" \"lists.gentoo.org\")) NIL NIL NIL \"<1199706209l.3020l.8l@(none)>\")",1024);
  
	t = imap_get_envelope(GMIME_MESSAGE(m->content));
	fail_unless(strcmp(t,expect)==0,"imap_get_envelope failed\n%s\n%s\n ", expect, t);
	
	g_free(t);
	g_free(expect);
	dbmail_message_free(m);

	/*  */
	m = dbmail_message_new();
	s = g_string_new(encoded_message_utf8);
	m = dbmail_message_init_with_string(m, s);
	g_string_free(s,TRUE);

	//strncpy(expect,"(\"Thu, 01 Jan 1970 00:00:00 +0000\" \"=?ISO-8859-2?Q?Re=3A_=5Bgentoo-dev=5D_New_developer=3A__?= =?ISO-8859-2?Q?Miroslav_=A9ulc_=28fordfrog=29?=\" ((\"=?ISO-8859-2?Q?=22Miroslav_=A9ulc_=28fordfrog=29=22?=\" NIL \"fordfrog\" \"gentoo.org\")) ((\"=?ISO-8859-2?Q?=22Miroslav_=A9ulc_=28fordfrog=29=22?=\" NIL \"fordfrog\" \"gentoo.org\")) ((\"=?ISO-8859-2?Q?=22Miroslav_=A9ulc_=28fordfrog=29=22?=\" NIL \"fordfrog\" \"gentoo.org\")) ((NIL NIL \"gentoo-dev\" \"lists.gentoo.org\")) NIL NIL NIL NIL)",1024);

	t = imap_get_envelope(GMIME_MESSAGE(m->content));
	//fail_unless(strcmp(t,expect)==0,"imap_get_envelope failed\n%s\n%s\n ", expect, t);
	
	g_free(t);
	//g_free(expect);
	dbmail_message_free(m);



}
END_TEST


START_TEST(test_imap_get_partspec)
{
	DbmailMessage *message;
	GMimeObject *object;
	char *result, *expect;
	
	/* text/plain */
	message = dbmail_message_new();
	message = dbmail_message_init_with_string(message, g_string_new(rfc822));

	object = imap_get_partspec(GMIME_OBJECT(message->content),"HEADER");
	result = imap_get_logical_part(object,"HEADER");
	//"From nobody Wed Sep 14 16:47:48 2005\n" 
	
	expect = g_strdup("Message-Id: <1199706209l.3020l.1l@(none)>\n"
			"To: testuser@foo.org\n"
			"From: somewher@foo.org\n"
			"Subject: dbmail test message\n"
			"MIME-Version: 1.0\n"
			"Content-Type: text/plain; charset=us-ascii\n"
			"Content-Transfer-Encoding: 7bit\n"
			"\n");

	fail_unless(MATCH(expect,result),"imap_get_partsec failed [%s] !=\n[%s]\n", expect, result);
	g_free(expect);
	g_free(result);

	object = imap_get_partspec(GMIME_OBJECT(message->content),"TEXT");
	result = imap_get_logical_part(object,"TEXT");
	fail_unless(strlen(result)==29,"imap_get_partspec failed");
	g_free(result);

	dbmail_message_free(message);

	/* multipart */
	
	message = dbmail_message_new();
	message = dbmail_message_init_with_string(message, g_string_new(multipart_message));

	object = imap_get_partspec(GMIME_OBJECT(message->content),"1");
	result = g_mime_object_to_string(object);
	expect = g_strdup("Content-type: text/html\n"
	        "Content-disposition: inline\n\n"
	        "Test message one");

	fail_unless(MATCH(expect,result),"imap_get_partspec failed:\n[%s] != \n[%s]\n", expect, result);
	g_free(result);
	g_free(expect);

	object = imap_get_partspec(GMIME_OBJECT(message->content),"1.TEXT");
	result = imap_get_logical_part(object,"TEXT");
	fail_unless(strlen(result)==16,"imap_get_partspec failed");
	g_free(result);

	object = imap_get_partspec(GMIME_OBJECT(message->content),"1.HEADER");
	result = imap_get_logical_part(object,"HEADER");
	fail_unless(strlen(result)==53,"imap_get_partspec failed");
	g_free(result);
	
	object = imap_get_partspec(GMIME_OBJECT(message->content),"2.MIME");
	result = imap_get_logical_part(object,"MIME");
	fail_unless(strlen(result)==93,"imap_get_partspec failed");
	g_free(result);

	dbmail_message_free(message);
	

	/* multipart mixed */

	message = dbmail_message_new();
	message = dbmail_message_init_with_string(message, g_string_new(multipart_mixed));

	object = imap_get_partspec(GMIME_OBJECT(message->content),"2.HEADER");
	result = imap_get_logical_part(object,"HEADER");
	fail_unless(strncmp(result,"From: \"try\" <try@test.kisise>",29)==0,"imap_get_partspec failed");
	g_free(result);

	object = imap_get_partspec(GMIME_OBJECT(message->content),"2.1.1");
	result = g_mime_object_to_string(object);
	expect = g_strdup("Content-Type: text/plain;\n"
		"	charset=\"us-ascii\"\n"
		"Content-Transfer-Encoding: 7bit\n\n"
		"Body of doc2\n\n");
	fail_unless(MATCH(expect,result),"imap_get_partspec failed:\n[%s] != \n[%s]\n", expect, result);
	g_free(result);
	g_free(expect);



	dbmail_message_free(message);

}
END_TEST

#ifdef OLD
static u64_t get_physid(void)
{
	u64_t id = 0;
	GString *q = g_string_new("");
	g_string_printf(q,"select id from %sphysmessage order by id desc limit 1", DBPFX);
	Connection_executeQuery(q->str);
	g_string_free(q,TRUE);
	id = db_get_result_u64(0,0);
	return id;
}
static u64_t get_msgsid(void)
{
	u64_t id = 0;
	GString *q = g_string_new("");
	g_string_printf(q,"select message_idnr from %smessages order by message_idnr desc limit 1", DBPFX);
	Connection_executeQuery(q->str);
	g_string_free(q,TRUE);
	id = db_get_result_u64(0,0);
	return id;
}
#endif

START_TEST(test_g_list_slices)
{
	unsigned i=0;
	unsigned j=98;
	unsigned s=11;
	GList *list = NULL;
	GList *sub = NULL;
	for (i=0; i< j; i++) 
		list = g_list_append_printf(list, "ELEM_%d", i);
	list = g_list_slices(list, s);
	list = g_list_first(list);
	fail_unless(g_list_length(list)==9, "number of slices incorrect");
	sub = g_string_split(g_string_new((gchar *)list->data), ",");
	fail_unless(g_list_length(sub)==s,"Slice length incorrect");

	g_list_foreach(list,(GFunc)g_free,NULL);
	g_list_foreach(sub,(GFunc)g_free,NULL);
	
	i=0;
	j=17;
	s=100;
	list = NULL;
	sub = NULL;
	for (i=0; i< j; i++) 
		list = g_list_append_printf(list, "ELEM_%d", i);
	list = g_list_slices(list, s);
	list = g_list_first(list);
	fail_unless(g_list_length(list)==1, "number of slices incorrect [%d]", g_list_length(list));
	sub = g_string_split(g_string_new((gchar *)list->data), ",");
	fail_unless(g_list_length(sub)==j,"Slice length incorrect");

}
END_TEST
START_TEST(test_g_list_slices_u64)
{
	unsigned i=0;
	unsigned j=98;
	unsigned s=11;
	u64_t *l;
	GList *list = NULL;
	GList *sub = NULL;
	for (i=0; i< j; i++) {
		l = g_new0(u64_t,1);
		*l = i;
		list = g_list_append(list, l);
	}
		
	list = g_list_slices_u64(list, s);
	list = g_list_first(list);
	fail_unless(g_list_length(list)==9, "number of slices incorrect");
	
	sub = g_string_split(g_string_new((gchar *)list->data), ",");
	fail_unless(g_list_length(sub)==s,"Slice length incorrect");

	g_list_foreach(list,(GFunc)g_free,NULL);
	g_list_foreach(sub,(GFunc)g_free,NULL);
	
	i=0;
	j=17;
	s=100;
	list = NULL;
	sub = NULL;
	for (i=0; i< j; i++) 
		list = g_list_append_printf(list, "ELEM_%d", i);
	list = g_list_slices(list, s);
	list = g_list_first(list);
	fail_unless(g_list_length(list)==1, "number of slices incorrect [%d]", g_list_length(list));
	sub = g_string_split(g_string_new((gchar *)list->data), ",");
	fail_unless(g_list_length(sub)==j,"Slice length incorrect");

}
END_TEST


unsigned int get_bound_lo(unsigned int * set, unsigned int setlen) {
	unsigned int index = 0;
	while (set[index]==0 && index < setlen) 
		index++;
	return index;
}
unsigned int get_bound_hi(unsigned int * set, unsigned int setlen) {
	int index = setlen;
	while (set[index]==0 && index >= 0) 
		index--;
	return (unsigned)index;
}
unsigned int get_count_on(unsigned int * set, unsigned int setlen) {
	unsigned int i, count = 0;
	for (i=0; i<setlen; i++)
		if (set[i])
			count++;
	return count;
}

	
static int wrap_base_subject(const char *in, const char *expect) 
{
	char *out = dm_base_subject(in);
	int res;
	res = strcmp(out, expect);
	g_free(out);
	return res;
}	

#define BS(x,y) fail_unless(wrap_base_subject((x),(y))==0, "dm_base_subject failed")
#define BSF(x,y) fail_unless(wrap_base_subject((x),(y))!=0, "dm_base_subject failed (negative)")

START_TEST(test_dm_base_subject)
{
	BS("Re: foo","foo");
	BS("Fwd: foo","foo");
	BS("Fw: foo","foo");
	BS("[issue123] foo","foo");
	BS("Re [issue123]: foo","foo");
	BS("Re: [issue123] foo","foo");
	BS("Re: [issue123] [Fwd: foo]","foo");
	BS("[Dbmail-dev] [DBMail 0000240]: some bug report","some bug report");

	BS("test\t\tspaces  here","test spaces here");
	BS("test strip trailer here (fwd) (fwd)","test strip trailer here");
	BS("Re: Fwd: [fwd: some silly test subject] (fwd)","some silly test subject");
	
	BSF("=?koi8-r?B?4snMxdTZIPcg5OXu+CDz8OXr9OHr7PEg9/Pl5+ThIOTs8SD34fMg8+8g8+vp5Ovv6iEg0yA=?=",
            "=?koi8-r?B?4snMxdTZIPcg5OXu+CDz8OXr9OHr7PEg9/Pl5+ThIOTs8SD34fMg8+8g8+vp5Ovv6iEg0yA=?=");


}
END_TEST

#define Y(z,q) fail_unless(z==(q), "listex_match failed")
#define X(z,a,b) Y(z,listex_match(a,b,".",0))
#define N(z,a,b) Y(z,listex_match(a,b,"�",0))
START_TEST(test_listex_match)
{
	X(1, "INBOX", "INBOX");
	X(0, "INBOX", "INBOX.Foo");
	X(0, "INBOX", "INBOX.Foo.Bar");

	X(0, "INBOX.%", "INBOX");
	X(1, "INBOX.%", "INBOX.Foo");
	X(0, "INBOX.%", "INBOX.Foo.Bar");

	X(0, "INBOX.*", "INBOX");
	X(1, "INBOX.*", "INBOX.Foo");
	X(1, "INBOX.*", "INBOX.Foo.Bar");

	X(1, "INBOX*", "INBOX");
	X(1, "INBOX*", "INBOX.Foo");
	X(1, "INBOX*", "INBOX.Foo.Bar");

	X(1, "%", "INBOX");
	X(0, "%.%", "INBOX");
	X(1, "%.%", "INBOX.Foo");
	X(0, "%.%.%", "INBOX.Foo");
	X(1, "%.%.%", "INBOX.Foo.Bar");

	X(1, "INBOX%", "INBOX");
	X(0, "INBOX%", "INBOX.Foo");
	X(0, "INBOX%", "INBOX.Foo.Bar");

	X(0, "INBOX*Foo", "INBOX");
	X(1, "INBOX*Foo", "INBOX.Foo");
	X(0, "INBOX*Foo", "INBOX.Foo.Bar");

	X(0, "INBOX*Bar", "INBOX");
	X(0, "INBOX*Bar", "INBOX.Foo");
	X(1, "INBOX*Bar", "INBOX.Foo.Bar");

	X(0, "INBOX.*Bar", "INBOX");
	X(0, "INBOX.*Bar", "INBOX.Foo");
	X(1, "INBOX.*Bar", "INBOX.Foo.Bar");

	N(0, "INBOX\317\200*Bar", "INBOX");
	N(0, "INBOX\317\200*Bar", "INBOX\317\200""Foo");
	N(1, "INBOX\317\200*Bar", "INBOX\317\200""Foo\317\200""Bar");

}
END_TEST

/* this test will fail if you're not in the CET timezone */
#define D(x,y) fail_unless(strncasecmp(date_sql2imap(x),y,IMAP_INTERNALDATE_LEN)==0,"date_sql2imap failed")
START_TEST(test_date_sql2imap)
{
//	printf("[%s]\n", date_sql2imap("2005-05-03 14:10:06"));
//	printf("[%s]\n", date_sql2imap("2005-01-03 14:10:06"));
        D("2005-05-03 14:10:06","03-May-2005 14:10:06 +0200");
        D("2005-01-03 14:10:06","03-Jan-2005 14:10:06 +0100");
}
END_TEST

Suite *dbmail_suite(void)
{
	Suite *s = suite_create("Dbmail Imap");
	TCase *tc_session = tcase_create("ImapSession");
	TCase *tc_mime = tcase_create("Mime");
	TCase *tc_util = tcase_create("Utils");
	TCase *tc_misc = tcase_create("Misc");
	
	suite_add_tcase(s, tc_session);
	suite_add_tcase(s, tc_mime);
	suite_add_tcase(s, tc_util);
	suite_add_tcase(s, tc_misc);
	
	tcase_add_checked_fixture(tc_session, setup, teardown);
	tcase_add_test(tc_session, test_imap_session_new);
	tcase_add_test(tc_session, test_imap_bodyfetch);
	tcase_add_test(tc_session, test_imap_get_structure);
	tcase_add_test(tc_session, test_imap_cleanup_address);
	tcase_add_test(tc_session, test_internet_address_parse_string);
	tcase_add_test(tc_session, test_imap_get_envelope);
	tcase_add_test(tc_session, test_imap_get_envelope_8bit_id);
	tcase_add_test(tc_session, test_imap_get_envelope_koi);
	tcase_add_test(tc_session, test_imap_get_envelope_latin);
	tcase_add_test(tc_session, test_imap_get_partspec);
	
	tcase_add_checked_fixture(tc_mime, setup, teardown);
	tcase_add_test(tc_mime, test_g_mime_object_get_body);

	tcase_add_checked_fixture(tc_util, setup, teardown);
	tcase_add_test(tc_util, test_dbmail_imap_plist_as_string);
	tcase_add_test(tc_util, test_dbmail_imap_plist_collapse);
	tcase_add_test(tc_util, test_dbmail_imap_astring_as_string);
	tcase_add_test(tc_util, test_g_list_slices);
	tcase_add_test(tc_util, test_g_list_slices_u64);
	tcase_add_test(tc_util, test_listex_match);
	tcase_add_test(tc_util, test_date_sql2imap);

	tcase_add_checked_fixture(tc_misc, setup, teardown);
	tcase_add_test(tc_misc, test_dm_base_subject);
	return s;
}

int main(void)
{
	int nf;
	g_mime_init(0);
	Suite *s = dbmail_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	g_mime_shutdown();
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
	
