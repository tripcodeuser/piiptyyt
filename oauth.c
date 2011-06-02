
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <glib.h>
#include <gcrypt.h>

#include "defs.h"


static const char *sig_method_str[] = {
	[SIG_HMAC_SHA1] = "HMAC-SHA1",
};


static char *copy(struct oauth_request *req, const char *str)
{
	if(str == NULL) return NULL;
	else return g_string_chunk_insert(req->strs, str);
}


void oa_req_free(struct oauth_request *req)
{
	g_string_chunk_free(req->strs);
	g_free(req);
}


struct oauth_request *oa_req_new(void)
{
	struct oauth_request *req = g_new0(struct oauth_request, 1);
	req->strs = g_string_chunk_new(512);

	return req;
}


struct oauth_request *oa_req_new_with_params(
	const char *consumer_key, const char *consumer_secret,
	const char *request_url, const char *request_method,
	int sig_method,
	const char *callback_url)
{
	struct oauth_request *req = oa_req_new();
	req->consumer_key = copy(req, consumer_key);
	req->consumer_secret = copy(req, consumer_secret);
	req->request_url = copy(req, request_url);
	req->request_method = copy(req, request_method);
	req->sig_method = sig_method;
	req->callback_url = copy(req, callback_url);

	return req;
}


void oa_set_token(
	struct oauth_request *req,
	const char *token,
	const char *secret)
{
	req->token_key = copy(req, token);
	req->token_secret = copy(req, secret);
}


void oa_set_verifier(struct oauth_request *req, const char *verifier)
{
	req->verifier = copy(req, verifier);
}


/* oauth_url_escape() and the base64 functions are originally from liboauth
 * 0.9.4, which was licensed under MIT-style conditions. these are compatible
 * with piiptyyt's GPLv3+ terms.
 */
static char *oauth_url_escape(const char *string) {
  size_t alloc, newlen;
  char *ns = NULL, *testing_ptr = NULL;
  unsigned char in; 
  size_t strindex=0;
  size_t length;

  if (!string) return g_strdup("");

  alloc = strlen(string)+1;
  newlen = alloc;

  ns = g_malloc(alloc);

  length = alloc-1;
  while(length--) {
    in = *string;

    switch(in){
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
    case 'a': case 'b': case 'c': case 'd': case 'e':
    case 'f': case 'g': case 'h': case 'i': case 'j':
    case 'k': case 'l': case 'm': case 'n': case 'o':
    case 'p': case 'q': case 'r': case 's': case 't':
    case 'u': case 'v': case 'w': case 'x': case 'y': case 'z':
    case 'A': case 'B': case 'C': case 'D': case 'E':
    case 'F': case 'G': case 'H': case 'I': case 'J':
    case 'K': case 'L': case 'M': case 'N': case 'O':
    case 'P': case 'Q': case 'R': case 'S': case 'T':
    case 'U': case 'V': case 'W': case 'X': case 'Y': case 'Z':
    case '_': case '~': case '.': case '-':
      ns[strindex++]=in;
      break;
    default:
      newlen += 2; /* this'll become a %XX */
      if(newlen > alloc) {
        alloc *= 2;
        testing_ptr = g_realloc(ns, alloc);
        ns = testing_ptr;
      }
      snprintf(&ns[strindex], 4, "%%%02X", in);
      strindex+=3;
      break;
    }
    string++;
  }
  ns[strindex]=0;
  return ns;
}


static char oauth_b64_encode(uint8_t u)
{
  if(u < 26)  return 'A'+u;
  if(u < 52)  return 'a'+(u-26);
  if(u < 62)  return '0'+(u-52);
  if(u == 62) return '+';
  return '/';
}


static char *oauth_encode_base64(size_t size, const uint8_t *src)
{
  int i;
  char *out, *p;

  if(!src) return NULL;
  if(!size) size= strlen((char *)src);
  out= g_malloc0(sizeof(char) * (size*4/3+4));
  p= out;

  for(i=0; i<size; i+=3) {
    unsigned char b1=0, b2=0, b3=0, b4=0, b5=0, b6=0, b7=0;
    b1= src[i];
    if(i+1<size) b2= src[i+1];
    if(i+2<size) b3= src[i+2];
      
    b4= b1>>2;
    b5= ((b1&0x3)<<4)|(b2>>4);
    b6= ((b2&0xf)<<2)|(b3>>6);
    b7= b3&0x3f;
      
    *p++= oauth_b64_encode(b4);
    *p++= oauth_b64_encode(b5);
      
    if(i+1<size) *p++= oauth_b64_encode(b6);
    else *p++= '=';
      
    if(i+2<size) *p++= oauth_b64_encode(b7);
    else *p++= '=';
  }
  return out;
}


static char *oa_sig_base(
	const char *method,
	const char *uri,
	GHashTable *params)
{
	/* produce a sorted list of keys and values, where keys and values are
	 * formatted as enc(k) . "=" . enc(v)
	 */
	GList *keys = g_hash_table_get_keys(params);
	for(GList *cur = g_list_first(keys);
		cur != NULL;
		cur = g_list_next(cur))
	{
		const char *key = cur->data,
			*value = g_hash_table_lookup(params, key);
		assert(value != NULL);
		char *e_key = oauth_url_escape(key),
			*e_val = oauth_url_escape(value);
		cur->data = g_strdup_printf("%s=%s", e_key, e_val);
		g_free(e_key);
		g_free(e_val);
	}
	keys = g_list_sort(keys, (GCompareFunc)&strcmp);

	GString *str = g_string_sized_new(512);
	g_string_append_printf(str, "%s&", method);
	char *e_uri = oauth_url_escape(uri);
	g_string_append_printf(str, "%s&", e_uri);
	g_free(e_uri);

	for(GList *cur = g_list_first(keys);
		cur != NULL;
		cur = g_list_next(cur))
	{
		if(cur != g_list_first(keys)) g_string_append(str, "%26");
		char *enc = oauth_url_escape(cur->data);
		g_string_append(str, enc);
		g_free(enc);
	}
	g_list_foreach(keys, (GFunc)&g_free, NULL);
	g_list_free(keys);

	return g_string_free(str, FALSE);
}


/* uses HMAC-SHA1 when "key" is given. when not, plain SHA1. */
static char *oa_sign_sha1(
	const void *sig_base,
	size_t sb_len,
	const char *key)
{
	gcry_md_hd_t md = NULL;
	gcry_error_t err = gcry_md_open(&md, GCRY_MD_SHA1,
		key != NULL ? GCRY_MD_FLAG_HMAC : 0);
	if(err != 0) goto fail;
	if(key != NULL) {
		err = gcry_md_setkey(md, key, strlen(key));
		if(err != 0) goto fail;
	}
	gcry_md_write(md, sig_base, sb_len > 0 ? sb_len : strlen(sig_base));
	const void *value = gcry_md_read(md, GCRY_MD_SHA1);
	assert(value != NULL);
	unsigned int val_len = gcry_md_get_algo_dlen(GCRY_MD_SHA1);
	char *ret = oauth_encode_base64(val_len, value);

	gcry_md_close(md);

	return ret;

fail:
	fprintf(stderr, "%s: gcrypt error: %s\n", __func__,
		gcry_strerror(err));
	if(md != NULL) gcry_md_close(md);
	return NULL;
}


static char *oa_gen_nonce(void)
{
	int fd = open("/dev/urandom", O_RDONLY);
	if(fd == -1) {
		/* FIXME: be more careful */
		fprintf(stderr, "%s: open(2) of /dev/urandom failed: %s\n",
			__func__, strerror(errno));
		abort();
	}
	uint8_t data[128];
	ssize_t n = read(fd, data, sizeof(data));
	if(n < sizeof(data)) {
		/* FIXME */
		fprintf(stderr, "%s: short read (got %zd, expected %zu)\n",
			__func__, n, sizeof(data));
		abort();
	}
	close(fd);
	return oa_sign_sha1(data, sizeof(data), NULL);
}


static GHashTable *gather_params(struct oauth_request *req, int kind)
{
	assert(req->sig_method < G_N_ELEMENTS(sig_method_str));

	if(req->consumer_key == NULL) return NULL;

	GHashTable *params = g_hash_table_new(&g_str_hash, &g_str_equal);
	switch(kind) {
	case OA_REQ_REQUEST_TOKEN:
		g_hash_table_insert(params, "oauth_callback",
			req->callback_url != NULL ? req->callback_url : "");
		break;

	case OA_REQ_ACCESS_TOKEN:
		if(req->verifier == NULL) req->verifier = copy(req, "");
		g_hash_table_insert(params, "oauth_verifier", req->verifier);
		g_hash_table_insert(params, "oauth_token", req->token_key);
		break;

	default:
		g_hash_table_destroy(params);
		return NULL;
	}

	g_hash_table_insert(params, "oauth_version", "1.0");
	g_hash_table_insert(params, "oauth_consumer_key", req->consumer_key);
	g_hash_table_insert(params, "oauth_signature_method",
		(void *)sig_method_str[req->sig_method]);
	if(req->timestamp == NULL) {
		char ts[32];
		struct timeval tv;
		gettimeofday(&tv, NULL);
		snprintf(ts, sizeof(ts), "%zu", (size_t)tv.tv_sec);
		req->timestamp = copy(req, ts);
	}
	g_hash_table_insert(params, "oauth_timestamp", req->timestamp);
	if(req->nonce == NULL) {
		char *nonce = oa_gen_nonce();
		req->nonce = copy(req, nonce);
		g_free(nonce);
	}
	g_hash_table_insert(params, "oauth_nonce", req->nonce);

	return params;
}


bool oa_sign_request(struct oauth_request *req, int kind)
{
	GHashTable *params = gather_params(req, kind);
	if(params == NULL) return false;

	char *sb = oa_sig_base(req->request_method, req->request_url, params);

	char *signature;
	switch(req->sig_method) {
	case SIG_HMAC_SHA1: {
		char *con_sec = oauth_url_escape(req->consumer_secret),
			*tok_sec = oauth_url_escape(req->token_secret),
			*hmac_key = g_strdup_printf("%s&%s", con_sec, tok_sec);
		signature = oa_sign_sha1(sb, 0, hmac_key);
		g_free(con_sec);
		g_free(tok_sec);
		g_free(hmac_key);
		break;
		}
	default:
		g_free(sb);
		g_hash_table_destroy(params);
		return false;
	}
	req->signature = copy(req, signature);

	g_free(signature);
	g_free(sb);
	g_hash_table_destroy(params);

	return true;
}


/* gather parameters, remove secrets, insert signature. common path for the
 * other parameter-consuming functions.
 */
static GHashTable *format_request_params(struct oauth_request *req, int kind)
{
	if(req->signature == NULL) return NULL;

	GHashTable *params = gather_params(req, kind);
	if(params == NULL) return NULL;

	/* remove secrets. */
	g_hash_table_remove(params, "oauth_consumer_secret");
	g_hash_table_remove(params, "oauth_token_secret");

	/* add stuff. */
	g_hash_table_insert(params, "oauth_signature", req->signature);

	return params;
}


const char *oa_auth_header(struct oauth_request *req, int kind)
{
	GHashTable *params = format_request_params(req, kind);
	if(params == NULL) return NULL;

	GString *str = g_string_sized_new(1024);
	g_string_append(str, "OAuth ");
	GHashTableIter iter;
	g_hash_table_iter_init(&iter, params);
	gpointer key, value;
	bool first = true;
	while(g_hash_table_iter_next(&iter, &key, &value)) {
		if(first) first = false; else g_string_append(str, ", ");
		char *e_key = oauth_url_escape(key),
			*e_val = oauth_url_escape(value);
		g_string_append_printf(str, "%s=\"%s\"", e_key, e_val);
		g_free(e_key);
		g_free(e_val);
	}

	g_hash_table_destroy(params);
	char *ret = copy(req, str->str);
	g_string_free(str, TRUE);
	return ret;
}


GHashTable *oa_request_token_params(struct oauth_request *req)
{
	return format_request_params(req, OA_REQ_REQUEST_TOKEN);
}


char *oa_request_params_to_post_body(struct oauth_request *req, int kind)
{
	GHashTable *params = format_request_params(req, kind);
	if(params == NULL) return NULL;

	GString *str = g_string_sized_new(256);
	GHashTableIter iter;
	g_hash_table_iter_init(&iter, params);
	gpointer k, v;
	bool first = true;
	while(g_hash_table_iter_next(&iter, &k, &v)) {
		assert(k != NULL);
		assert(v != NULL);
		if(first) first = false; else g_string_append_c(str, '&');
		char *ek = oauth_url_escape(k), *ev = oauth_url_escape(v);
		g_string_append_printf(str, "%s=%s", ek, ev);
		g_free(ek);
		g_free(ev);
	}

	g_hash_table_destroy(params);
	char *ret = copy(req, str->str);
	g_string_free(str, TRUE);
	return ret;
}


#define MAX_FIELDS 64

char **oa_parse_response(const char *body, ...)
{
	va_list al;
	va_start(al, body);
	const char *fields[MAX_FIELDS];
	int num_fields = 0;
	do {
		fields[num_fields++] = va_arg(al, const char *);
	} while(fields[num_fields - 1] != NULL && num_fields < MAX_FIELDS);
	num_fields--;
	va_end(al);

	char **pieces = g_strsplit(body, "&", 0);
	char **output = g_new0(char *, num_fields + 1);
	for(int i=0; pieces[i] != NULL; i++) {
		char *eq = strchr(pieces[i], '=');
		if(eq == NULL) {
			fprintf(stderr, "%s: warning: unknown response component `%s'\n",
				__func__, pieces[i]);
			continue;
		}
		*(eq++) = '\0';

		for(int j=0; j<num_fields; j++) {
			if(strcmp(fields[j], pieces[i]) == 0) {
				g_free(output[j]);
				output[j] = g_strdup(eq);
				break;
			}
		}
	}
	g_strfreev(pieces);

	return output;
}
