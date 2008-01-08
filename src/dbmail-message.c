/*
  

  Copyright (c) 2004-2006 NFG Net Facilities Group BV support@nfg.nl

  This program is free software; you can redistribute it and/or 
  modify it under the terms of the GNU General Public License 
  as published by the Free Software Foundation; either 
  version 2 of the License, or (at your option) any later 
  version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/**
 * \file dbmail-message.c
 *
 * implements DbmailMessage object
 */

#include "dbmail.h"


extern db_param_t _db_params;
#define DBPFX _db_params.pfx

#define MESSAGE_MAX_LINE_SIZE 1024

#define DBMAIL_TEMPMBOX "INBOX"

#define THIS_MODULE "message"
/*
 * _register_header
 *
 * register a message header in a ghashtable dictionary
 *
 */
static void _register_header(const char *header, const char *value, gpointer user_data);
static gboolean _header_cache(const char *header, const char *value, gpointer user_data);

static struct DbmailMessage * _retrieve(struct DbmailMessage *self, const char *query_template);
static void _map_headers(struct DbmailMessage *self);
static int _set_content(struct DbmailMessage *self, const GString *content);
static int _set_content_from_stream(struct DbmailMessage *self, GMimeStream *stream, dbmail_stream_t type);
static int _message_insert(struct DbmailMessage *self, 
		u64_t user_idnr, 
		const char *mailbox, 
		const char *unique_id); 


/* general mime utils (missing from gmime?) */

unsigned find_end_of_header(const char *h)
{
	gchar c, p1 = 0, p2 = 0;
	unsigned i = 0;
	size_t l;

	assert(h);

	l  = strlen(h);

	while (h++ && i<=l) {
		i++;
		c = *h;
		if (c == '\n' && ((p1 == '\n') || (p1 == '\r' && p2 == '\n'))) {
			if (l > i) 
				i++;
			break;
		}
		p2 = p1;
		p1 = c;
	}
	return i;
}


gchar * g_mime_object_get_body(const GMimeObject *object)
{
	gchar *s = NULL, *b = NULL;
        unsigned i;
	size_t l;
	
	g_return_val_if_fail(object != NULL, NULL);

	s = g_mime_object_to_string(GMIME_OBJECT(object));
	assert(s);

	i = find_end_of_header(s);
	if (i >= strlen(s))
		return NULL;
	
	b = s+i;
	l = strlen(b);
	memmove(s,b,l);
	s[l] = '\0';
	s = g_realloc(s, l+1);

	return s;
}

gchar * get_crlf_encoded_opt(const gchar *string, int dots)
{
	GMimeStream *ostream, *fstream;
	GMimeFilter *filter;
	gchar *encoded, *buf;
	GString *raw;
	
	ostream = g_mime_stream_mem_new();
	fstream = g_mime_stream_filter_new_with_stream(ostream);
	if (dots) {
		filter = g_mime_filter_crlf_new(GMIME_FILTER_CRLF_ENCODE,GMIME_FILTER_CRLF_MODE_CRLF_DOTS);
	} else {
		filter = g_mime_filter_crlf_new(GMIME_FILTER_CRLF_ENCODE,GMIME_FILTER_CRLF_MODE_CRLF_ONLY);
	}
	g_mime_stream_filter_add((GMimeStreamFilter *) fstream, filter);
	g_mime_stream_write_string(fstream,string);
	
	g_object_unref(filter);
	g_object_unref(fstream);
	
	g_mime_stream_reset(ostream);

	raw = g_string_new("");
	buf = g_new0(char,256);
	while ((g_mime_stream_read(ostream, buf, 255)) > 0) {
		raw = g_string_append(raw, buf);
		memset(buf,'\0', 256);
	}
	
	g_object_unref(ostream);
	
	encoded = raw->str;
	g_string_free(raw,FALSE);
	g_free(buf);
	
	return encoded;

}

static u64_t blob_exists(const char *buf, const char *hash)
{
	int i, rows;
	u64_t id = 0;
	const char *data;
	char query[DEF_QUERYSIZE];
	size_t len;
	assert(buf);

	memset(query,0,DEF_QUERYSIZE);
	snprintf(query, DEF_QUERYSIZE, "SELECT id,data FROM %smimeparts WHERE hash='%s'", DBPFX, hash);
	if (db_query(query) == DM_EQUERY)
		TRACE(TRACE_FATAL,"Unable to select from mimeparts table");
	
	len = strlen(buf);
	rows = db_num_rows();
	for (i=0; i< rows; i++) {
		data = db_get_result(i,1);
		if (memcmp(buf, data, len)==0) {
			id = db_get_result_u64(i,0);
			break;
		}
	}

	return id;
}

static u64_t blob_insert(const char *buf, const char *hash)
{
	GString *q;
	char *safe = NULL;
	u64_t id = 0;

	assert(buf);
	q = g_string_new("");
	safe = dm_strbinesc(buf);
	g_string_printf(q, "INSERT INTO %smimeparts (hash, data, size) VALUES ("
			"'%s', '%s', %zd)", DBPFX, hash, safe, strlen(buf));
	g_free(safe);

	if (db_query(q->str) == DM_EQUERY) {
		g_string_free(q,TRUE);
		return 0;
	}

	id = db_insert_result("mimeparts_id");

	db_free_result();
	g_string_free(q,TRUE);

	return id;
}

static int register_blob(struct DbmailMessage *m, u64_t id, gboolean is_header)
{
	GString *q;

	q = g_string_new("");
	g_string_printf(q, "INSERT INTO %spartlists "
		"(physmessage_id, is_header, part_key, part_depth, part_order, part_id) "
		"VALUES (%llu,%u,%u,%u,%u,%llu)", 
		DBPFX, dbmail_message_get_physid(m), is_header, 
		m->part_key, m->part_depth, m->part_order, id);

	if (db_query(q->str) == DM_EQUERY) {
		g_string_free(q,TRUE);
		return DM_EQUERY;
	}

	db_free_result();
	g_string_free(q,TRUE);

	return DM_SUCCESS;
}
static u64_t blob_store(const char *buf)
{
	u64_t id;
	char *hash = dm_get_hash_for_string(buf);

	// store this message fragment
	if ((id = blob_exists(buf, (const char *)hash)) != 0) {
		g_free(hash);
		return id;
	}

	if ((id = blob_insert(buf, (const char *)hash)) != 0) {
		g_free(hash);
		return id;
	}
	
	return 0;
}

static int _dm_store_blob(struct DbmailMessage *m, const char *buf, gboolean is_header)
{
	u64_t id;
	assert(buf);

	if (is_header) {
		m->part_key++;
		m->part_order=0;
	}

	if (! (id = blob_store(buf)))
		return DM_EQUERY;

	// register this message fragment
	if (register_blob(m, id, is_header) == DM_EQUERY)
		return DM_EQUERY;

	m->part_order++;

	return 0;

}
static const char * find_boundary(const char *s)
{
	GMimeContentType *type;
	char header[128];
	const char *boundary;
	char *rest;
	int i=0, j=0;

	memset(header,0,sizeof(header));

	rest = g_strcasestr(s, "Content-type:");
	if (! rest)
		return NULL;

	i = 13;
	while (rest[i]) {
		if (((rest[i] == '\n') || (rest[i] == '\r')) && (!isspace(rest[i+1]))) {
			break;
		}
		header[j++]=rest[i++];
	}
	header[j]='\0';
	g_strstrip(header);
	type = g_mime_content_type_new_from_string(header);
	boundary = g_mime_content_type_get_parameter(type,"boundary");
        g_free(type);
	
	return boundary;
}


static struct DbmailMessage * _mime_retrieve(struct DbmailMessage *self)
{
	char *str;
	const char *boundary = NULL;
	const char *internal_date = NULL;
	char **blist = g_new0(char *,32);
	int prevdepth, depth = 0, order, row, rows;
	int key = 1;
	gboolean got_boundary = FALSE, prev_boundary = FALSE;
	gboolean is_header = TRUE, prev_header;
	char query[DEF_QUERYSIZE];
	memset(query,0,DEF_QUERYSIZE);
	GString *m;

	assert(dbmail_message_get_physid(self));

	snprintf(query, DEF_QUERYSIZE, "SELECT data,l.part_key,l.part_depth,l.part_order,l.is_header,%s "
		"FROM %smimeparts p "
		"JOIN %spartlists l ON p.id = l.part_id "
		"JOIN %sphysmessage ph ON ph.id = l.physmessage_id "
		"WHERE l.physmessage_id = %llu ORDER BY l.part_key,l.part_order ASC", 
		date2char_str("ph.internal_date"), DBPFX, DBPFX, DBPFX, dbmail_message_get_physid(self));

	if (db_query(query) == -1) {
		TRACE(TRACE_ERROR, "sql error");
		return NULL;
	}
	
	rows = db_num_rows();
	if (rows < 1) {
		db_free_result();
		return NULL;	/* msg should have 1 block at least */
	}

	m = g_string_new("");

	for (row=0; row < rows; row++) {

		prevdepth = depth;
		prev_header = is_header;

		str = (char *)db_get_result(row,0);
		key = db_get_result_int(row,1);
		depth = db_get_result_int(row,2);
		order = db_get_result_int(row,3);
		is_header = db_get_result_bool(row,4);
		if (row == 0)
			internal_date = db_get_result(row,5);

		if (is_header)
			prev_boundary = got_boundary;

		got_boundary = FALSE;
		if (is_header && ((boundary = find_boundary(str)) != NULL)) {
			got_boundary = TRUE;
			blist[depth] = (char *)boundary;
		}

		if (prevdepth > depth && blist[depth]) {
			g_string_append_printf(m, "\n--%s--\n", blist[depth]);
			blist[depth] = NULL;
		}

		if (depth>0)
			boundary = (const char *)blist[depth-1];

		if (is_header && (!prev_header|| prev_boundary))
			g_string_append_printf(m, "\n--%s\n", boundary);

		g_string_append_printf(m, "%s", str);
		if (is_header)
			g_string_append_printf(m,"\n");
	}
	if (rows > 1 && boundary) {
		g_string_append_printf(m, "\n--%s--\n", boundary);
		boundary = NULL;
	}

	if (rows > 1 && depth > 0 && blist[0])
		g_string_append_printf(m, "\n--%s--\n\n", blist[0]);

	db_free_result();

	self = dbmail_message_init_with_string(self,m);
	if (strlen(internal_date))
		dbmail_message_set_internal_date(self, (char *)internal_date);
	g_string_free(m,TRUE);
	g_free(blist);

	return self;
}

static gboolean _dm_store_mime_object(GMimeObject *object, struct DbmailMessage *m, gboolean descend);

static gboolean store_mime_object(GMimeObject *object, struct DbmailMessage *m)
{
	return _dm_store_mime_object(object,m,TRUE);
}

static void store_head(GMimeObject *object, struct DbmailMessage *m)
{
	char *head = g_mime_object_get_headers(object);
	_dm_store_blob(m, head, 1);
	g_free(head);
}

static void store_body(GMimeObject *object, struct DbmailMessage *m)
{
	char *text = g_mime_object_get_body(object);
	if (! text)
		return;

	_dm_store_blob(m, text, 0);
	g_free(text);
}


static gboolean _dm_store_mime_text(GMimeObject *object, struct DbmailMessage *m)
{
	g_return_val_if_fail(GMIME_IS_OBJECT(object), TRUE);

	store_head(object, m);
	store_body(object, m);

	return FALSE;
}

static gboolean _dm_store_mime_multipart(GMimeObject *object, struct DbmailMessage *m, const GMimeContentType *content_type)
{
	char *text;
	size_t i = 0, t;
	const char *boundary;
	int n;

	g_return_val_if_fail(GMIME_IS_OBJECT(object), TRUE);

	boundary = g_mime_content_type_get_parameter(content_type,"boundary");

	store_head(object,m);

	if (g_mime_content_type_is_type(content_type, "multipart", "mixed")) {
		// store the pre-amble
		text = g_mime_object_get_body(object);
		if (text) {
			t = strlen(text);

			while(t>2 && i++ < t && text[i+2]) { // until first -- boundary
				if ((i==0 || text[i] == '\n') && text[i+1]=='-' && text[i+2]=='-')
					break;
			}

			if (i>0 && i<t) {
				text[i] = '\0';
				text = g_realloc(text,i);
				_dm_store_blob(m, text, 0);
			}

			g_free(text);
		}
	}

	if (boundary) {
		m->part_depth++;
		n = m->part_order;
		m->part_order=0;
	}
	g_mime_multipart_foreach((GMimeMultipart *)object, (GMimePartFunc)store_mime_object, m);

	if (boundary) {
		n++;
		m->part_depth--;
		m->part_order=n;
	}

	return FALSE;
}

static gboolean _dm_store_mime_message(GMimeObject * object, struct DbmailMessage *m)
{
	GMimeMessage *m2;

	store_head(object, m);

	m2 = g_mime_message_part_get_message(GMIME_MESSAGE_PART(object));

	g_return_val_if_fail(GMIME_IS_MESSAGE(m2), TRUE);

	_dm_store_mime_object(GMIME_OBJECT(m2), m, TRUE);

	g_object_unref(m2);
	
	return FALSE;
	
}

gboolean _dm_store_mime_object(GMimeObject *object, struct DbmailMessage *m, gboolean descend)
{
	const GMimeContentType *content_type;
	GMimeObject *mime_part;
	gboolean r = FALSE;

	g_return_val_if_fail(GMIME_IS_OBJECT(object), TRUE);

	if (descend && GMIME_IS_MESSAGE(object))
		mime_part = g_mime_message_get_mime_part((GMimeMessage *)object);
	else
		mime_part = object;

	content_type = g_mime_object_get_content_type(mime_part);

	if (g_mime_content_type_is_type(content_type, "multipart", "*"))
		r = _dm_store_mime_multipart((GMimeObject *)mime_part, m, content_type);

	else if (g_mime_content_type_is_type(content_type, "message","*"))
		r = _dm_store_mime_message((GMimeObject *)mime_part, m);

	else 
		r = _dm_store_mime_text((GMimeObject *)mime_part, m);

	if (descend && GMIME_IS_MESSAGE(object))
		g_object_unref(mime_part);

	return r;
}


static gboolean _dm_message_store(struct DbmailMessage *m)
{
	return _dm_store_mime_object((GMimeObject *)m->content, m, FALSE);
}


/* Useful for debugging. Uncomment if/when needed.
 *//*
static void dump_to_file(const char *filename, const char *buf)
{
	gint se;
	g_assert(filename);
	FILE *f = fopen(filename,"a");
	if (! f) {
		se=errno;
		TRACE(TRACE_DEBUG,"opening dumpfile failed [%s]", strerror(se));
		errno=se;
		return;
	}
	fprintf(f,"%s",buf);
	fclose(f);
}
*/


/*  \brief create a new empty DbmailMessage struct
 *  \return the DbmailMessage
 */

struct DbmailMessage * dbmail_message_new(void)
{
	struct DbmailMessage *self = g_new0(struct DbmailMessage,1);
	
	self->envelope_recipient = g_string_new("");

	/* provide quick case-insensitive header name searches */
	self->header_name = g_tree_new((GCompareFunc)g_ascii_strcasecmp);
	/* provide quick case-sensitive header value searches */
	self->header_value = g_tree_new((GCompareFunc)strcmp);
	
	
	/* internal cache: header_dict[headername.name] = headername.id */
	self->header_dict = g_hash_table_new_full((GHashFunc)g_str_hash,
			(GEqualFunc)g_str_equal, (GDestroyNotify)g_free, NULL);
	
	dbmail_message_set_class(self, DBMAIL_MESSAGE);
	
	return self;
}

void dbmail_message_free(struct DbmailMessage *self)
{
	if (! self)
		return;

	if (self->headers)
		g_relation_destroy(self->headers);
	if (self->content)
		g_object_unref(self->content);
	if (self->raw)
		g_byte_array_free(self->raw,TRUE);
	if (self->charset)
		g_free(self->charset);

	self->headers=NULL;
	self->content=NULL;
	self->raw=NULL;
	self->charset=NULL;
	
	g_string_free(self->envelope_recipient,TRUE);
	g_hash_table_destroy(self->header_dict);
	g_tree_destroy(self->header_name);
	g_tree_destroy(self->header_value);
	
	self->id=0;
	g_free(self);
}


/* \brief create and initialize a new DbmailMessage
 * \param FILE *instream from which to read
 * \param int streamtype is DBMAIL_STREAM_PIPE or DBMAIL_STREAM_LMTP
 * \return the new DbmailMessage
 */
struct DbmailMessage * dbmail_message_new_from_stream(FILE *instream, int streamtype) 
{
	
	GMimeStream *stream;
	struct DbmailMessage *message, *retmessage;
	
	assert(instream);
	message = dbmail_message_new();
	stream = g_mime_stream_fs_new(dup(fileno(instream)));
	retmessage = dbmail_message_init_with_stream(message, stream, streamtype);
	g_object_unref(stream);

	if (retmessage)
		return retmessage;
	
	dbmail_message_free(message);
	return NULL;
}

/* \brief set the type flag for this DbmailMessage
 * \param the DbmailMessage on which to set the flag
 * \param type flag is either DBMAIL_MESSAGE or DBMAIL_MESSAGE_PART
 * \return non-zero in case of error
 */
int dbmail_message_set_class(struct DbmailMessage *self, int klass)
{
	switch (klass) {
		case DBMAIL_MESSAGE:
		case DBMAIL_MESSAGE_PART:
			self->klass = klass;
			break;
		default:
			return 1;
			break;
	}		
	return 0;
			
}

/* \brief accessor for the type flag
 * \return the flag
 */
int dbmail_message_get_class(const struct DbmailMessage *self)
{
	return self->klass;
}

/* \brief initialize a previously created DbmailMessage using a GString
 * \param the empty DbmailMessage
 * \param GString *content contains the raw message
 * \return the filled DbmailMessage
 */
struct DbmailMessage * dbmail_message_init_with_string(struct DbmailMessage *self, const GString *content)
{

	_set_content(self,content);

	if (! (GMIME_IS_MESSAGE(self->content))) {
		dbmail_message_set_class(self, DBMAIL_MESSAGE_PART);
		g_object_unref(self->content);
		self->content=NULL;
		_set_content(self, content);
	}
	
	_map_headers(self);
	
	return self;
}

struct DbmailMessage * dbmail_message_init_from_gmime_message(struct DbmailMessage *self, GMimeMessage *message)
{
	g_return_val_if_fail(GMIME_IS_MESSAGE(message), NULL);

	self->content = GMIME_OBJECT(message);
	_map_headers(self);

	return self;

}

/* \brief initialize a previously created DbmailMessage using a GMimeStream
 * \param empty DbmailMessage
 * \param stream from which to read
 * \param type which indicates either pipe/network style streaming
 * \return the filled DbmailMessage
 */
struct DbmailMessage * dbmail_message_init_with_stream(struct DbmailMessage *self, GMimeStream *stream, dbmail_stream_t type)
	{
	int res;

	res = _set_content_from_stream(self,stream,type);
	if (res != 0)
		return NULL;

	_map_headers(self);
	return self;
}

static int _set_content(struct DbmailMessage *self, const GString *content)
{
	int res;
	GMimeStream *stream;

	if (self->raw) {
		g_byte_array_free(self->raw,TRUE);
		self->raw = NULL;
	}
	
	self->raw = g_byte_array_new();
	self->raw = g_byte_array_append(self->raw,(guint8 *)content->str, content->len+1);
	//stream = g_mime_stream_mem_new_with_byte_array(self->raw);
	stream = g_mime_stream_mem_new_with_buffer(content->str, content->len+1);
	res = _set_content_from_stream(self, stream, DBMAIL_STREAM_PIPE);
	g_mime_stream_close(stream);
	g_object_unref(stream);
	return res;
}

static int _set_content_from_stream(struct DbmailMessage *self, GMimeStream *stream, dbmail_stream_t type)
{
	/* 
	 * We convert all messages to crlf->lf for internal usage and
	 * db-insertion
	 */
	
	GMimeStream *fstream, *bstream, *mstream;
	GMimeFilter *filter;
	GMimeParser *parser;
	gchar *buf, *from = NULL;
	ssize_t getslen, putslen;
	FILE *tmp;
	int res = 0;
	gboolean firstline=TRUE;

	/*
	 * buildup the memory stream buffer
	 * we will read from stream until either EOF or <dot><crlf> is encountered
	 * depending on the streamtype
	 */

	if (self->content) {
		g_object_unref(self->content);
		self->content=NULL;
	}
	
	parser = g_mime_parser_new();
		
	switch(type) {
		case DBMAIL_STREAM_LMTP:
		case DBMAIL_STREAM_PIPE:
			
			buf = g_new0(char, MESSAGE_MAX_LINE_SIZE);

			// stream -> bstream (buffer) -> fstream (filter) -> mstream (in-memory copy)
			bstream = g_mime_stream_buffer_new(stream,GMIME_STREAM_BUFFER_BLOCK_READ);
//			mstream = g_mime_stream_mem_new();

			tmp = tmpfile(); 
			mstream = g_mime_stream_file_new(tmp);

			assert(mstream);
			fstream = g_mime_stream_filter_new_with_stream(mstream);
			filter = g_mime_filter_crlf_new(GMIME_FILTER_CRLF_DECODE,GMIME_FILTER_CRLF_MODE_CRLF_DOTS);
			g_mime_stream_filter_add((GMimeStreamFilter *) fstream, filter);
			
			while ((getslen = g_mime_stream_buffer_gets(bstream, buf, MESSAGE_MAX_LINE_SIZE)) > 0) {
				if (firstline && strncmp(buf,"From ",5)==0) {
					from = g_strdup(buf);
					firstline=FALSE;
					continue;
				}

				if ((type==DBMAIL_STREAM_LMTP) && (strncmp(buf,".\r\n",3)==0))
					break;

				putslen = g_mime_stream_write(fstream, buf, getslen);

				if (g_mime_stream_flush(fstream)) {
					TRACE(TRACE_ERROR, "Failed to flush, is your /tmp filesystem full?");
					res = 1;
					break;
				}

				if (putslen < getslen && getslen > putslen+1) {
					TRACE(TRACE_ERROR, "Short write [%zd < %zd], is your /tmp filesystem full?", 
						putslen, getslen);
					res = 1;
					break;
				}
			}

			if (getslen < 0) {
				TRACE(TRACE_ERROR, "Read failed, did the client drop the connection?");
				res = 1;
			}

			g_free(buf);
			
			g_mime_stream_reset(mstream);
			g_mime_parser_init_with_stream(parser, mstream);

			g_object_unref(filter);
			g_object_unref(fstream);
			g_object_unref(bstream);
			g_object_unref(mstream);

		break;

		default:
		case DBMAIL_STREAM_RAW:
			g_mime_parser_init_with_stream(parser, stream);
		break;

	}

	switch (dbmail_message_get_class(self)) {
		case DBMAIL_MESSAGE:
			TRACE(TRACE_DEBUG,"parse message");
			self->content = GMIME_OBJECT(g_mime_parser_construct_message(parser));
			// adding a header will prime the gmime message structure, but we want
			// to add an innocuous header
			dbmail_message_set_header(self,"MIME-Version","1.0"); 
			if (from) {
				dbmail_message_set_internal_date(self, from);
				g_free(from);
			}
			break;
		case DBMAIL_MESSAGE_PART:
			TRACE(TRACE_DEBUG,"parse part");
			self->content = GMIME_OBJECT(g_mime_parser_construct_part(parser));
			break;
	}

	g_object_unref(parser);

	return res;
}

static void _map_headers(struct DbmailMessage *self) 
{
	GMimeObject *part;
	assert(self->content);
	self->headers = g_relation_new(2);
	g_relation_index(self->headers, 0, (GHashFunc)g_str_hash, (GEqualFunc)g_str_equal);
	g_relation_index(self->headers, 1, (GHashFunc)g_str_hash, (GEqualFunc)g_str_equal);

	 // gmime doesn't consider the content-type header to be a message-header so extract 
	 // and register it separately
	if (GMIME_IS_MESSAGE(self->content)) {
		char *type = NULL;
		part = g_mime_message_get_mime_part(GMIME_MESSAGE(self->content));
		if ((type = (char *)g_mime_object_get_header(part,"Content-Type"))!=NULL)
			_register_header("Content-Type",type, (gpointer)self);
		g_object_unref(part);
	}

	g_mime_header_foreach(GMIME_OBJECT(self->content)->headers, _register_header, self);
}

static void _register_header(const char *header, const char *value, gpointer user_data)
{
	const char *hname, *hvalue;
	struct DbmailMessage *m = (struct DbmailMessage *)user_data;
	if (! (hname = g_tree_lookup(m->header_name,header))) {
		g_tree_insert(m->header_name,(gpointer)header,(gpointer)header);
		hname = header;
	}
	if (! (hvalue = g_tree_lookup(m->header_value,value))) {
		g_tree_insert(m->header_value,(gpointer)value,(gpointer)value);
		hvalue = value;
	}
	
	if (! g_relation_exists(m->headers, hname, hvalue))
		g_relation_insert(m->headers, hname, hvalue);
}

void dbmail_message_set_physid(struct DbmailMessage *self, u64_t physid)
{
	self->physid = physid;
}

u64_t dbmail_message_get_physid(const struct DbmailMessage *self)
{
	return self->physid;
}

void dbmail_message_set_internal_date(struct DbmailMessage *self, char *internal_date)
{
	if (internal_date)
		self->internal_date = g_mime_utils_header_decode_date(internal_date, self->internal_date_gmtoff);
}

/* thisyear is a workaround for some broken gmime version. */
gchar * dbmail_message_get_internal_date(const struct DbmailMessage *self, int thisyear)
{
	char *res;
	struct tm gmt;
	if (! self->internal_date)
		return NULL;
	
	res = g_new0(char, TIMESTRING_SIZE+1);
	memset(&gmt,'\0', sizeof(struct tm));
	gmtime_r(&self->internal_date, &gmt);

	/* override if the date is not sane */
	if (thisyear && gmt.tm_year + 1900 > thisyear + 1) {
		gmt.tm_year = thisyear - 1900;
	}

	strftime(res, TIMESTRING_SIZE, "%Y-%m-%d %T", &gmt);
	return res;
}

void dbmail_message_set_envelope_recipient(struct DbmailMessage *self, const char *envelope_recipient)
{
	if (envelope_recipient)
		g_string_printf(self->envelope_recipient,"%s", envelope_recipient);
}

gchar * dbmail_message_get_envelope_recipient(const struct DbmailMessage *self)
{
	if (self->envelope_recipient->len > 0)
		return self->envelope_recipient->str;
	return NULL;
}

void dbmail_message_set_header(struct DbmailMessage *self, const char *header, const char *value)
{
	g_mime_message_set_header(GMIME_MESSAGE(self->content), header, value);
}

const gchar * dbmail_message_get_header(const struct DbmailMessage *self, const char *header)
{
	return g_mime_message_get_header(GMIME_MESSAGE(self->content), header);
}

GTuples * dbmail_message_get_header_repeated(const struct DbmailMessage *self, const char *header)
{
	const char *hname;
	if (! (hname = g_tree_lookup(self->header_name,header)))
		hname = header;
	return g_relation_select(self->headers, hname, 0);
}

GList * dbmail_message_get_header_addresses(struct DbmailMessage *message, const char *field_name)
{
	InternetAddressList *ialisthead, *ialist;
	InternetAddress *ia;
	GList *result = NULL;
	const char *field_value;

	if (!message || !field_name) {
		TRACE(TRACE_WARNING, "received a NULL argument, this is a bug");
		return NULL; 
	}

	field_value = dbmail_message_get_header(message, field_name);
	TRACE(TRACE_INFO, "mail address parser looking at field [%s] with value [%s]", field_name, field_value);
	
	if ((ialist = internet_address_parse_string(field_value)) == NULL) {
		TRACE(TRACE_MESSAGE, "mail address parser error parsing header field");
		return NULL;
	}

	ialisthead = ialist;
	while (1) {
		ia = ialist->address;
		result = g_list_append(result, g_strdup(ia->value.addr));
		if (! ialist->next)
			break;
		ialist = ialist->next;
	}
	
	internet_address_list_destroy(ialisthead);

	TRACE(TRACE_DEBUG, "mail address parser found [%d] email addresses", g_list_length(result));

	return result;
}
char * dbmail_message_get_charset(struct DbmailMessage *self)
{
	if (! self->charset)
		self->charset = message_get_charset((GMimeMessage *)self->content);
	return self->charset;
}

/* dump message(parts) to char ptrs */
gchar * dbmail_message_to_string(const struct DbmailMessage *self) 
{
	return g_mime_object_to_string(GMIME_OBJECT(self->content));
}
gchar * dbmail_message_body_to_string(const struct DbmailMessage *self)
{
	return g_mime_object_get_body(GMIME_OBJECT(self->content));
}
gchar * dbmail_message_hdrs_to_string(const struct DbmailMessage *self)
{
	gchar *h;
	unsigned i = 0;

//	i = find_end_of_header((const char *)self->raw->data);
//	h = g_strndup((const char *)self->raw->data, i);

	h = dbmail_message_to_string(self);
	i = find_end_of_header(h);
	h[i] = '\0';
	h = g_realloc(h, strlen(h)+1);

	return h;
}


/* 
 * Some dynamic accessors.
 * 
 * Don't cache these values to allow changes in message content!!
 * 
 */
size_t dbmail_message_get_size(const struct DbmailMessage *self, gboolean crlf)
{
	char *s, *t; size_t r;
	s = dbmail_message_to_string(self);

        if (crlf) {
		t = get_crlf_encoded(s);
		r = strlen(t);
		g_free(t);
	} else {
		r = strlen(s);
	}
	
	g_free(s);
	return r;
}
size_t dbmail_message_get_hdrs_size(const struct DbmailMessage *self, gboolean crlf)
{
	char *s, *t; size_t r;
	s = dbmail_message_hdrs_to_string(self);

	if (crlf) {
	        t = get_crlf_encoded(s);
		r = strlen(t);
        	g_free(t);
	} else {
		r = strlen(s);
	}
	
	g_free(s);
	return r;
}
size_t dbmail_message_get_body_size(const struct DbmailMessage *self, gboolean crlf)
{
	char *s, *t; size_t r;
	s = dbmail_message_body_to_string(self);

	if (! s) return 0;

	if (crlf) {
		t = get_crlf_encoded(s);
		r = strlen(t);
        	g_free(t);
	} else {
		r = strlen(s);
	}
	
	g_free(s);
	return r;
}


static struct DbmailMessage * _retrieve(struct DbmailMessage *self, const char *query_template)
{
	
	int row = 0, rows = 0;
	GString *m;
	char query[DEF_QUERYSIZE];
	memset(query,0,DEF_QUERYSIZE);
	struct DbmailMessage *store;
	const char *internal_date;
	
	assert(dbmail_message_get_physid(self));
	
	store = self;

	if ((self = _mime_retrieve(self)))
		return self;

	self = store;

	snprintf(query, DEF_QUERYSIZE, query_template, date2char_str("p.internal_date"), 
		DBPFX, DBPFX, dbmail_message_get_physid(self));

	if (db_query(query) == -1) {
		TRACE(TRACE_ERROR, "sql error");
		return NULL;
	}
	
	rows = db_num_rows();
	if (rows < 1) {
		TRACE(TRACE_ERROR, "blk error");
		db_free_result();
		return NULL;	/* msg should have 1 block at least */
	}

	m = g_string_new("");
	for (row=0; row < rows; row++) {
		char *str = (char *)db_get_result(row,0);
		if (row == 0)
			internal_date = db_get_result(row,2);

		g_string_append_printf(m, "%s", str);
	}
	db_free_result();
	
	self = dbmail_message_init_with_string(self,m);
	if (strlen(internal_date))
		dbmail_message_set_internal_date(self, (char *)internal_date);

	g_string_free(m,TRUE);

	return self;
}

/*
 *
 * retrieve the header messageblk
 *
 * TODO: this call is yet unused in the code, but here for
 * forward compatibility's sake.
 *
 */
static struct DbmailMessage * _fetch_head(struct DbmailMessage *self)
{
	const char *query_template = 	"SELECT b.messageblk, b.is_header, %s "
		"FROM %smessageblks b "
		"JOIN %sphysmessage p ON b.physmessage_id=p.id "
		"WHERE b.physmessage_id = %llu "
		"AND b.is_header = '1'";
	return _retrieve(self, query_template);

}

/*
 *
 * retrieve the full message
 *
 */
static struct DbmailMessage * _fetch_full(struct DbmailMessage *self) 
{
	const char *query_template = "SELECT b.messageblk, b.is_header, %s "
		"FROM %smessageblks b "
		"JOIN %sphysmessage p ON b.physmessage_id=p.id "
		"WHERE b.physmessage_id = %llu "
		"ORDER BY b.messageblk_idnr";
	return _retrieve(self, query_template);
}

/* \brief retrieve message
 * \param empty DbmailMessage
 * \param physmessage_id
 * \param filter (header-only or full message)
 * \return filled DbmailMessage
 */
struct DbmailMessage * dbmail_message_retrieve(struct DbmailMessage *self, u64_t physid, int filter)
{
	assert(physid);
	
	dbmail_message_set_physid(self, physid);
	
	switch (filter) {
		case DBMAIL_MESSAGE_FILTER_HEAD:
			self = _fetch_head(self);
			break;

		case DBMAIL_MESSAGE_FILTER_BODY:
		case DBMAIL_MESSAGE_FILTER_FULL:
			self = _fetch_full(self);
			break;
	}
	

	if ((!self) || (! self->content)) {
		TRACE(TRACE_ERROR, "retrieval failed for physid [%llu]", physid);
		return NULL;
	}

	return self;
}


/* \brief store a temporary copy of a message.
 * \param 	filled DbmailMessage
 * \return 
 *     - -1 on error
 *     -  1 on success
 */
int dbmail_message_store(struct DbmailMessage *self)
{
	u64_t user_idnr;
	u64_t messageblk_idnr;
	char unique_id[UID_SIZE];
	char *hdrs, *body;
	u64_t hdrs_size, body_size, rfcsize;
	char *domainname;
	char *message_id;
	int retry=4, delay=200;
	
	if (auth_user_exists(DBMAIL_DELIVERY_USERNAME, &user_idnr) <= 0) {
		TRACE(TRACE_ERROR, "unable to find user_idnr for user [%s]. Make sure this system user is in the database!", DBMAIL_DELIVERY_USERNAME);
		return DM_EQUERY;
	}
	
	create_unique_id(unique_id, user_idnr);

	while (retry-- > 0) {
		db_begin_transaction();

		/* create a message record */
		if(_message_insert(self, user_idnr, DBMAIL_TEMPMBOX, unique_id) < 0) {
			db_rollback_transaction();
			usleep(delay);
			continue;
		}

		/* make sure the message has a message-id, else threading breaks */
		if (! (message_id = (char *)g_mime_message_get_message_id(GMIME_MESSAGE(self->content)))) {
			domainname = g_new0(gchar, 255);
			if (getdomainname(domainname,255))
				strcpy(domainname,"(none)");
			message_id = g_mime_utils_generate_message_id(domainname);
			g_mime_message_set_message_id(GMIME_MESSAGE(self->content), message_id);
			g_free(message_id);
			g_free(domainname);
		}

		hdrs = dbmail_message_hdrs_to_string(self);
		body = dbmail_message_body_to_string(self);

		hdrs_size = (u64_t)dbmail_message_get_hdrs_size(self, FALSE);
		body_size = (u64_t)dbmail_message_get_body_size(self, FALSE);

		if (_dm_message_store(self)) {
			TRACE(TRACE_FATAL,"Failed to store mimeparts");
			db_rollback_transaction();
			usleep(delay);

// deprecated code. Just here to apeace the linker (??)

			/* store body in several blocks (if needed */
			if(db_insert_message_block(hdrs, hdrs_size, self->id, &messageblk_idnr,1) < 0) {
				g_free(hdrs);
				g_free(body);
				return -1;
			}

			if (store_message_in_blocks(body, body_size, self->id) < 0) {
				g_free(hdrs);
				g_free(body);
				return -1;
			}

			g_free(hdrs);
			g_free(body);
// deprecated--/>

			db_rollback_transaction();
			usleep(delay);
			continue;

		}

		rfcsize = (u64_t)dbmail_message_get_rfcsize(self);
		if (db_update_message(self->id, unique_id, (hdrs_size + body_size), rfcsize) < 0) {
			db_rollback_transaction();
			usleep(delay);
			continue;
		}

		/* store message headers */
		if (dbmail_message_cache_headers(self) < 0) {
			db_rollback_transaction();
			usleep(delay);
			continue;
			}
		
		/* ready for commit */
		break;
	}

	return db_commit_transaction();
}

int _message_insert(struct DbmailMessage *self, 
		u64_t user_idnr, 
		const char *mailbox, 
		const char *unique_id)
{
	u64_t mailboxid;
	u64_t physmessage_id;
	char *internal_date = NULL;
	char query[DEF_QUERYSIZE];
	memset(query,0,DEF_QUERYSIZE);

	assert(unique_id);
	assert(mailbox);

	if (db_find_create_mailbox(mailbox, BOX_DEFAULT, user_idnr, &mailboxid) == -1)
		return -1;
	
	if (mailboxid == 0) {
		TRACE(TRACE_ERROR, "mailbox [%s] could not be found!", mailbox);
		return -1;
	}

	/* get the messages date, but override it if it's from the future */
	struct timeval tv;
	struct tm gmt;
	int thisyear;
	gettimeofday(&tv, NULL);
	localtime_r(&tv.tv_sec, &gmt);
	thisyear = gmt.tm_year + 1900;
	internal_date = dbmail_message_get_internal_date(self, thisyear);
	int result = 0;

	/* insert a new physmessage entry */
	if (db_insert_physmessage_with_internal_date(internal_date, &physmessage_id) == -1)  {
		g_free(internal_date);
		return -1;
	}
	g_free(internal_date);

	dbmail_message_set_physid(self, physmessage_id);

	/* now insert an entry into the messages table */
	snprintf(query, DEF_QUERYSIZE, "INSERT INTO "
			"%smessages(mailbox_idnr, physmessage_id, unique_id,"
			"recent_flag, status) "
			"VALUES (%llu, %llu, '%s', 1, %d)",
			DBPFX, mailboxid, physmessage_id, unique_id,
			MESSAGE_STATUS_INSERT);

	if ((result = db_query(query)) == DM_EQUERY) {
		TRACE(TRACE_ERROR,"inserting message failed");
		return result;
	}

	self->id = db_insert_result("message_idnr");
	TRACE(TRACE_DEBUG,"new message_idnr [%llu]", self->id);
	db_free_result();

	return result;
}

int dbmail_message_cache_headers(const struct DbmailMessage *self)
{
	assert(self);
	assert(self->physid);

	if (! GMIME_IS_MESSAGE(self->content)) {
		TRACE(TRACE_ERROR,"self->content is not a message");
		return -1;
	}

	g_tree_foreach(self->header_name, (GTraverseFunc)_header_cache, (gpointer)self);
	
	dbmail_message_cache_tofield(self);
	dbmail_message_cache_ccfield(self);
	dbmail_message_cache_fromfield(self);
	dbmail_message_cache_datefield(self);
	dbmail_message_cache_replytofield(self);
	dbmail_message_cache_subjectfield(self);
	dbmail_message_cache_referencesfield(self);
	dbmail_message_cache_envelope(self);

	return DM_SUCCESS;
}
#define CACHE_WIDTH_VALUE 255
#define CACHE_WIDTH_FIELD 255
#define CACHE_WIDTH_ADDR 100
#define CACHE_WIDTH_NAME 100


static int _header_get_id(const struct DbmailMessage *self, const char *header, u64_t *id)
{
	u64_t tmp;
	gpointer cacheid;
	gchar *case_header;
	gchar *safe_header;
	gchar *tmpheader;
	int try=3;

	// rfc822 headernames are case-insensitive
	if (! (tmpheader = dm_strnesc(header,CACHE_WIDTH_NAME)))
		return -1;
	safe_header = g_ascii_strdown(tmpheader,-1);
	g_free(tmpheader);

	cacheid = g_hash_table_lookup(self->header_dict, (gconstpointer)safe_header);
	if (cacheid) {
		*id = GPOINTER_TO_UINT(cacheid);
		g_free(safe_header);
		return 1;
	}
		
	GString *q = g_string_new("");

	case_header = g_strdup_printf(db_get_sql(SQL_STRCASE),"headername");

	while (try-- > 0) { // deal with race conditions from other process inserting the same headername
		db_savepoint("header_id");	
		g_string_printf(q, "SELECT id FROM %sheadername WHERE %s='%s'", DBPFX, case_header, safe_header);
		g_free(case_header);

		if (db_query(q->str) == -1) {
			db_savepoint_rollback("header_id");
			g_string_free(q,TRUE);
			g_free(safe_header);
			return -1;
		}
		if (db_num_rows() < 1) {
			db_free_result();
			g_string_printf(q, "INSERT %s INTO %sheadername (headername) VALUES ('%s')", 
				db_get_sql(SQL_IGNORE), DBPFX, safe_header);
			if (db_query(q->str) == -1) {
				db_savepoint_rollback("header_id");
			} else {
				tmp = db_insert_result("headername_idnr");
			}
		} else {
			tmp = db_get_result_u64(0,0);
			db_free_result();
		}
		if (tmp) break;
		usleep(200);
	}

	*id = tmp;
	g_hash_table_insert(self->header_dict, (gpointer)(g_strdup(safe_header)), GUINT_TO_POINTER((unsigned)tmp));
	g_free(safe_header);
	g_string_free(q,TRUE);
	return 1;
}

static gboolean _header_cache(const char UNUSED *key, const char *header, gpointer user_data)
{
	u64_t id;
	struct DbmailMessage *self = (struct DbmailMessage *)user_data;
	gchar *safe_value;
	GString *q;
	GTuples *values;
	unsigned char *raw;
	unsigned i;
	gboolean isaddr = 0;

	/* skip headernames with spaces like From_ */
	if (strchr(header, ' '))
		return FALSE;

	if ((_header_get_id(self, header, &id) < 0))
		return TRUE;

	if (g_ascii_strcasecmp(header,"From")==0)
		isaddr=1;
	else if (g_ascii_strcasecmp(header,"To")==0)
		isaddr=1;
	else if (g_ascii_strcasecmp(header,"Reply-to")==0)
		isaddr=1;
	else if (g_ascii_strcasecmp(header,"Cc")==0)
		isaddr=1;
	else if (g_ascii_strcasecmp(header,"Bcc")==0)
		isaddr=1;

	q = g_string_new("");
	values = g_relation_select(self->headers,header,0);
	for (i=0; i<values->len;i++) {
		raw = (unsigned char *)g_tuples_index(values,i,1);
		TRACE(TRACE_DEBUG,"raw header value [%s]", raw);
		
 		char *value = NULL;
  		const char *charset = dbmail_message_get_charset(self);
  
 		value = dbmail_iconv_decode_field((const char *)raw, charset, isaddr);
 
 		if ((! value) || (strlen(value) == 0))
 			continue;
 
 		safe_value = dm_stresc(value);
 		g_free(value);

		g_string_printf(q,"INSERT INTO %sheadervalue (headername_id, physmessage_id, headervalue) "
				"VALUES (%llu,%llu,'%s')", DBPFX, id, self->physid, safe_value);
		g_free(safe_value);
		safe_value = NULL;

		if (db_query(q->str)) {
			TRACE(TRACE_ERROR,"insert headervalue failed");
			g_string_free(q,TRUE);
			g_tuples_destroy(values);
			return TRUE;
		}
	}
	g_string_free(q,TRUE);
	g_tuples_destroy(values);
	return FALSE;
}

static void insert_address_cache(u64_t physid, const char *field, InternetAddressList *ialist, const struct DbmailMessage *self)
{
	InternetAddress *ia;
	
	g_return_if_fail(ialist != NULL);

	GString *q = g_string_new("");
	gchar *name, *rname;
	gchar *addr;
	char *charset = dbmail_message_get_charset((struct DbmailMessage *)self);

	for (; ialist != NULL && ialist->address; ialist = ialist->next) {
		
		ia = ialist->address;
		g_return_if_fail(ia != NULL);

		rname=dbmail_iconv_str_to_db(ia->name ? ia->name: "", charset);
		/* address fields are truncated to column width */
		name = dm_strnesc(rname, CACHE_WIDTH_ADDR);
		addr = dm_strnesc(ia->value.addr ? ia->value.addr : "", CACHE_WIDTH_ADDR);

		g_string_printf(q, "INSERT INTO %s%sfield (physmessage_id, %sname, %saddr) "
				"VALUES (%llu,'%s','%s')", DBPFX, field, field, field, 
				physid, name, addr);
		
		g_free(name);
		g_free(addr);
		g_free(rname);
		
		if (db_query(q->str)) {
			TRACE(TRACE_ERROR, "insert %sfield failed [%s]", field, q->str);
		}

	}
	
	g_string_free(q,TRUE);
}

static void insert_field_cache(u64_t physid, const char *field, const char *value)
{
	GString *q;
	gchar *clean_value;

	g_return_if_fail(value != NULL);

	/* field values are truncated to 255 bytes */
	clean_value = dm_strnesc(value,CACHE_WIDTH_FIELD);

	q = g_string_new("");

	g_string_printf(q, "INSERT INTO %s%sfield (physmessage_id, %sfield) "
			"VALUES (%llu,'%s')", DBPFX, field, field, physid, clean_value);

	g_free(clean_value);

	if (db_query(q->str)) {
		TRACE(TRACE_ERROR, "insert %sfield failed [%s]", field, q->str);
	}
	g_string_free(q,TRUE);
}
#define DM_ADDRESS_TYPE_TO "To"
#define DM_ADDRESS_TYPE_CC "Cc"
#define DM_ADDRESS_TYPE_FROM "From"
#define DM_ADDRESS_TYPE_REPL "Reply-to"

static InternetAddressList * dm_message_get_addresslist(const struct DbmailMessage *self, const char * type)
{
	const char *addr = NULL;
	char *charset = NULL;
	char *value = NULL;
	InternetAddressList *list;

	if (! (addr = (char *)dbmail_message_get_header(self, type)))
		return NULL;

	charset = dbmail_message_get_charset((struct DbmailMessage *)self);
	value = dbmail_iconv_decode_field(addr, charset, TRUE);
	list = internet_address_parse_string(value);

	g_free(value);

	return list;
}

void dbmail_message_cache_tofield(const struct DbmailMessage *self)
{
	InternetAddressList *list;

	if (! (list = dm_message_get_addresslist(self, DM_ADDRESS_TYPE_TO)))
		return;
	insert_address_cache(self->physid, "to", list,self);
	internet_address_list_destroy(list);
}

void dbmail_message_cache_ccfield(const struct DbmailMessage *self)
{
	InternetAddressList *list;

	if (! (list = dm_message_get_addresslist(self, DM_ADDRESS_TYPE_CC)))
		return;
	insert_address_cache(self->physid, "cc", list,self);
	internet_address_list_destroy(list);

}

void dbmail_message_cache_fromfield(const struct DbmailMessage *self)
{
	InternetAddressList *list;

	if (! (list = dm_message_get_addresslist(self, DM_ADDRESS_TYPE_FROM)))
		return;
	insert_address_cache(self->physid, "from", list,self);
	internet_address_list_destroy(list);
}

void dbmail_message_cache_replytofield(const struct DbmailMessage *self)
{
	InternetAddressList *list;

	if (! (list = dm_message_get_addresslist(self, DM_ADDRESS_TYPE_REPL)))
		return;
	insert_address_cache(self->physid, "replyto", list,self);
	internet_address_list_destroy(list);
}


void dbmail_message_cache_datefield(const struct DbmailMessage *self)
{
	char *value;
	time_t date;

	if (! (value = (char *)dbmail_message_get_header(self,"Date")))
		date = (time_t)0;
	else
		date = g_mime_utils_header_decode_date(value,NULL);
	
	if (date == (time_t)-1)
		date = (time_t)0;

	value = g_new0(char,20);
	strftime(value,20,"%Y-%m-%d %H:%M:%S",gmtime(&date));

	insert_field_cache(self->physid, "date", value);
	
	g_free(value);
}

void dbmail_message_cache_subjectfield(const struct DbmailMessage *self)
{
	char *value, *raw, *s, *tmp;
	char *charset;
	
	charset = dbmail_message_get_charset((struct DbmailMessage *)self);

	// g_mime_message_get_subject fails to get 8-bit header, so we use dbmail_message_get_header
	raw = (char *)dbmail_message_get_header(self, "Subject");

	if (! raw) {
		TRACE(TRACE_MESSAGE,"no subject field value [%llu]", self->physid);
		return;
	}


	value = dbmail_iconv_str_to_utf8(raw, charset);
	s = dm_base_subject(value);
	
	// dm_base_subject returns utf-8 string, convert it into database encoding
	tmp = dbmail_iconv_str_to_db(s, charset);
	insert_field_cache(self->physid, "subject", tmp);
	g_free(tmp);
	g_free(s);
	
	g_free(value);
}

void dbmail_message_cache_referencesfield(const struct DbmailMessage *self)
{
	GMimeReferences *refs, *head;
	GTree *tree;
	const char *field;

	field = (char *)dbmail_message_get_header(self,"References");
	if (! field)
		field = dbmail_message_get_header(self,"In-Reply-to");
	if (! field) 
		return;

	refs = g_mime_references_decode(field);
	if (! refs) {
		TRACE(TRACE_MESSAGE, "reference_decode failed [%llu]", self->physid);
		return;
	}
	
	head = refs;
	tree = g_tree_new_full((GCompareDataFunc)strcmp, NULL, NULL, NULL);
	
	while (refs->msgid) {
		
		if (! g_tree_lookup(tree,refs->msgid)) {
			insert_field_cache(self->physid, "references", refs->msgid);
			g_tree_insert(tree,refs->msgid,refs->msgid);
		}

		if (refs->next == NULL)
			break;
		refs = refs->next;
	}

	g_tree_destroy(tree);
	g_mime_references_clear(&head);
}
	
void dbmail_message_cache_envelope(const struct DbmailMessage *self)
{
	char *q, *envelope, *clean;

	envelope = imap_get_envelope(GMIME_MESSAGE(self->content));
	clean = dm_stresc(envelope);

	q = g_strdup_printf("INSERT INTO %senvelope (physmessage_id, envelope) "
			"VALUES (%llu,'%s')", DBPFX, self->physid, clean);

	g_free(clean);
	g_free(envelope);

	if (db_query(q)) {
		TRACE(TRACE_ERROR, "insert envelope failed [%s]", q);
	}
	g_free(q);

}

// 
// construct a new message where only sender, recipient, subject and 
// a body are known. The body can be any kind of charset. Make sure
// it's not pre-encoded (base64, quopri)
//
// TODO: support text/html

struct DbmailMessage * dbmail_message_construct(struct DbmailMessage *self, 
		const gchar *to, const gchar *from, 
		const gchar *subject, const gchar *body)
{
	GMimeMessage *message;
	GMimePart *mime_part;
	GMimeDataWrapper *content;
	GMimeStream *stream, *fstream;
	GMimeContentType *mime_type;
	GMimePartEncodingType encoding = GMIME_PART_ENCODING_DEFAULT;
	GMimeFilter *filter = NULL;

	// FIXME: this could easily be expanded to allow appending
	// a new sub-part to an existing mime-part. But for now let's
	// require self to be a pristine (empty) DbmailMessage.
	g_return_val_if_fail(self->content==NULL, self);
	
	message = g_mime_message_new(FALSE);

	// determine the optimal encoding type for the body: how would gmime
	// encode this string. This will return either base64 or quopri.
	if (g_mime_utils_text_is_8bit((unsigned char *)body, strlen(body)))
		encoding = g_mime_utils_best_encoding((unsigned char *)body, strlen(body));

	// set basic headers
	g_mime_message_set_sender(message, from);
	g_mime_message_set_subject(message, subject);
	g_mime_message_add_recipients_from_string(message, GMIME_RECIPIENT_TYPE_TO, to);

	// construct mime-part
	mime_part = g_mime_part_new();
	
	// setup a stream-filter
	stream = g_mime_stream_mem_new();
	fstream = g_mime_stream_filter_new_with_stream(stream);
	
	switch(encoding) {
		case GMIME_PART_ENCODING_BASE64:
			filter = g_mime_filter_basic_new_type(GMIME_FILTER_BASIC_BASE64_ENC);
			break;
		case GMIME_PART_ENCODING_QUOTEDPRINTABLE:
			filter = g_mime_filter_basic_new_type(GMIME_FILTER_BASIC_QP_ENC);
			break;
		default:
			break;
	}

	if (filter) {
		g_mime_stream_filter_add((GMimeStreamFilter *)fstream, filter);
		g_object_unref(filter);
	}
	
	// fill the stream and thus the mime-part
	g_mime_stream_write_string(fstream,body);
	content = g_mime_data_wrapper_new_with_stream(stream, encoding);
	g_mime_part_set_content_object(mime_part, content);
	
	// add the correct mime-headers
	
	// Content-Type
	mime_type = g_mime_content_type_new("text","plain");
	g_mime_object_set_content_type((GMimeObject *)mime_part, mime_type);
	// We originally tried to use g_mime_charset_best to pick a charset,
	// but it regularly failed to choose utf-8 when utf-8 data was given to it.
	g_mime_object_set_content_type_parameter((GMimeObject *)mime_part, "charset", "utf-8");

	// Content-Transfer-Encoding
	switch(encoding) {
		case GMIME_PART_ENCODING_BASE64:
			g_mime_part_set_content_header(mime_part,"Content-Transfer-Encoding", "base64");
			break;
		case GMIME_PART_ENCODING_QUOTEDPRINTABLE:
			g_mime_part_set_content_header(mime_part,"Content-Transfer-Encoding", "quoted-printable");
			break;
		default:
			g_mime_part_set_content_header(mime_part,"Content-Transfer-Encoding", "7bit");
			break;
	}

	// attach the mime-part to the mime-message
	g_mime_message_set_mime_part(message, (GMimeObject *)mime_part);

	// attach the message to the DbmailMessage struct
	self->content = (GMimeObject *)message;

	// cleanup
	g_object_unref(mime_part);
	g_object_unref(content);
	g_object_unref(stream);
	g_object_unref(fstream);
	return self;
}


/* old stuff moved here from dbmsgbuf.c */

struct DbmailMessage * db_init_fetch(u64_t msg_idnr, int filter)
{
	struct DbmailMessage *msg;

	int result;
	u64_t physid = 0;
	if ((result = db_get_physmessage_id(msg_idnr, &physid)) != DM_SUCCESS)
		return NULL;
	msg = dbmail_message_new();
	if (! (msg = dbmail_message_retrieve(msg, physid, filter)))
		return NULL;

	return msg;
}