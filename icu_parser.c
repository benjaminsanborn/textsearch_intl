#include "postgres.h"

#include "fmgr.h"
#include "mb/pg_wchar.h"
#include "nodes/parsenodes.h"
#include "nodes/pg_list.h"
#include "tsearch/ts_public.h"
#include "utils/builtins.h"
#include "utils/memutils.h"

#include <unicode/ubrk.h>
#include <unicode/unorm2.h>
#include <unicode/ustring.h>
#include <unicode/utext.h>
#include <unicode/uversion.h>
#include <unicode/uchar.h>

PG_MODULE_MAGIC;

/*
 * Token ids match the default parser's word (2), blank (12) and uint (22) so
 * pg_catalog.prsd_headline, which hardcodes the default parser's ids, treats
 * blanks as spaces.
 */
#define ICU_WORD 2
#define ICU_BLANK 12
#define ICU_NUMBER 22
#define ICU_KANA 24
#define ICU_IDEO 25

typedef struct IcuParserState
{
	char	   *buf;
	int32		start;
	UText	   *text;
	UBreakIterator *iter;
	bool		owns_iter;
	MemoryContextCallback cleanup;
} IcuParserState;

/*
 * One iterator is reused for every document in the backend: setting new text
 * on it is much cheaper than cloning, which redoes per-script setup (about
 * 14 µs per document for Hangul).
 */
static UBreakIterator *shared_iter = NULL;
static bool shared_iter_in_use = false;

static void
check_icu(UErrorCode status, const char *what)
{
	if (U_FAILURE(status))
		ereport(ERROR,
				(errmsg("icu_parser: %s failed: %s", what, u_errorName(status))));
}

static void
close_icu(void *arg)
{
	IcuParserState *state = (IcuParserState *) arg;

	if (state->iter && state->owns_iter)
		ubrk_close(state->iter);
	else if (state->iter)
		shared_iter_in_use = false;
	if (state->text)
		utext_close(state->text);
	state->iter = NULL;
	state->text = NULL;
}

PG_FUNCTION_INFO_V1(icu_prsstart);
Datum
icu_prsstart(PG_FUNCTION_ARGS)
{
	char	   *buf = (char *) PG_GETARG_POINTER(0);
	int32		len = PG_GETARG_INT32(1);
	IcuParserState *state;
	UErrorCode	status = U_ZERO_ERROR;

	if (GetDatabaseEncoding() != PG_UTF8)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("icu_parser requires a UTF8 database")));

	if (shared_iter == NULL)
	{
		shared_iter = ubrk_open(UBRK_WORD, "", NULL, 0, &status);
		check_icu(status, "ubrk_open");
	}

	state = palloc0(sizeof(IcuParserState));
	state->buf = buf;
	state->cleanup.func = close_icu;
	state->cleanup.arg = state;
	MemoryContextRegisterResetCallback(CurrentMemoryContext, &state->cleanup);

	state->text = utext_openUTF8(NULL, buf, len, &status);
	check_icu(status, "utext_openUTF8");
	if (shared_iter_in_use)
	{
#if U_ICU_VERSION_MAJOR_NUM >= 69
		state->iter = ubrk_clone(shared_iter, &status);
#else
		state->iter = ubrk_safeClone(shared_iter, NULL, NULL, &status);
#endif
		check_icu(status, "ubrk_clone");
		state->owns_iter = true;
	}
	else
	{
		state->iter = shared_iter;
		shared_iter_in_use = true;
	}
	ubrk_setUText(state->iter, state->text, &status);
	check_icu(status, "ubrk_setUText");
	state->start = ubrk_first(state->iter);

	PG_RETURN_POINTER(state);
}

PG_FUNCTION_INFO_V1(icu_prsgettoken);
Datum
icu_prsgettoken(PG_FUNCTION_ARGS)
{
	IcuParserState *state = (IcuParserState *) PG_GETARG_POINTER(0);
	char	  **token = (char **) PG_GETARG_POINTER(1);
	int		   *token_len = (int *) PG_GETARG_POINTER(2);
	int32		end = ubrk_next(state->iter);
	int32		rule;

	if (end == UBRK_DONE)
		PG_RETURN_INT32(0);

	*token = state->buf + state->start;
	*token_len = end - state->start;
	state->start = end;

	rule = ubrk_getRuleStatus(state->iter);
	if (rule < UBRK_WORD_NONE_LIMIT)
		PG_RETURN_INT32(ICU_BLANK);
	if (rule < UBRK_WORD_NUMBER_LIMIT)
		PG_RETURN_INT32(ICU_NUMBER);
	if (rule < UBRK_WORD_LETTER_LIMIT)
		PG_RETURN_INT32(ICU_WORD);
	if (rule < UBRK_WORD_KANA_LIMIT)
		PG_RETURN_INT32(ICU_KANA);
	if (rule < UBRK_WORD_IDEO_LIMIT)
		PG_RETURN_INT32(ICU_IDEO);
	PG_RETURN_INT32(ICU_BLANK);
}

PG_FUNCTION_INFO_V1(icu_prsend);
Datum
icu_prsend(PG_FUNCTION_ARGS)
{
	close_icu(PG_GETARG_POINTER(0));
	PG_RETURN_VOID();
}

static void
set_lexdescr(LexDescr *descr, int id, const char *alias, const char *description)
{
	descr->lexid = id;
	descr->alias = pstrdup(alias);
	descr->descr = pstrdup(description);
}

PG_FUNCTION_INFO_V1(icu_prslextype);
Datum
icu_prslextype(PG_FUNCTION_ARGS)
{
	LexDescr   *descr = palloc0(sizeof(LexDescr) * 6);

	set_lexdescr(&descr[0], ICU_WORD, "word", "Word, letters");
	set_lexdescr(&descr[1], ICU_BLANK, "blank", "Space, punctuation and symbols");
	set_lexdescr(&descr[2], ICU_NUMBER, "number", "Number");
	set_lexdescr(&descr[3], ICU_KANA, "kana", "Kana word");
	set_lexdescr(&descr[4], ICU_IDEO, "ideo", "Ideographic word");

	PG_RETURN_POINTER(descr);
}

PG_FUNCTION_INFO_V1(icu_parser_icu_version);
Datum
icu_parser_icu_version(PG_FUNCTION_ARGS)
{
	UVersionInfo version;
	char		buf[U_MAX_VERSION_STRING_LENGTH];

	u_getVersion(version);
	u_versionToString(version, buf);
	PG_RETURN_TEXT_P(cstring_to_text(buf));
}

/*
 * NFKC splits the Thai and Lao vowel sara am into nikhahit + sara aa, which
 * ICU's dictionaries don't recognize, so the pair is recombined afterwards.
 */
static int32
recombine_and_strip(UChar *buf, int32 len)
{
	int32		out = 0;

	for (int32 i = 0; i < len; i++)
	{
		UChar		c = buf[i];

		if (c == 0x0027 || c == 0x2019)
			continue;
		if (i + 1 < len && c == 0x0E4D && buf[i + 1] == 0x0E32)
		{
			buf[out++] = 0x0E33;
			i++;
			continue;
		}
		if (i + 1 < len && c == 0x0ECD && buf[i + 1] == 0x0EB2)
		{
			buf[out++] = 0x0EB3;
			i++;
			continue;
		}
		buf[out++] = c;
	}
	return out;
}

static char *
normalize_token(const char *in, int32 len)
{
	UErrorCode	status = U_ZERO_ERROR;
	const UNormalizer2 *nfkc;
	UChar	   *src;
	UChar	   *dst;
	int32		src_len;
	int32		dst_len;
	int32		out_len;
	char	   *out;
	bool		ascii = true;
	bool		apostrophe = false;

	for (int32 i = 0; i < len; i++)
	{
		if ((unsigned char) in[i] >= 0x80)
			ascii = false;
		else if (in[i] == '\'')
			apostrophe = true;
	}
	if (ascii && !apostrophe)
		return NULL;
	if (ascii)
	{
		out = palloc(len + 1);
		out_len = 0;
		for (int32 i = 0; i < len; i++)
			if (in[i] != '\'')
				out[out_len++] = in[i];
		out[out_len] = '\0';
		return out;
	}

	u_strFromUTF8(NULL, 0, &src_len, in, len, &status);
	if (status != U_BUFFER_OVERFLOW_ERROR)
		check_icu(status, "u_strFromUTF8");
	status = U_ZERO_ERROR;
	src = palloc(sizeof(UChar) * (src_len + 1));
	u_strFromUTF8(src, src_len + 1, NULL, in, len, &status);
	check_icu(status, "u_strFromUTF8");

	nfkc = unorm2_getNFKCInstance(&status);
	check_icu(status, "unorm2_getNFKCInstance");
	if (unorm2_quickCheck(nfkc, src, src_len, &status) == UNORM_YES)
	{
		dst = src;
		dst_len = src_len;
	}
	else
	{
		status = U_ZERO_ERROR;
		dst_len = unorm2_normalize(nfkc, src, src_len, NULL, 0, &status);
		if (status != U_BUFFER_OVERFLOW_ERROR)
			check_icu(status, "unorm2_normalize");
		status = U_ZERO_ERROR;
		dst = palloc(sizeof(UChar) * (dst_len + 1));
		unorm2_normalize(nfkc, src, src_len, dst, dst_len + 1, &status);
	}
	check_icu(status, "unorm2_normalize");

	out_len = recombine_and_strip(dst, dst_len);
	if (out_len == src_len && (dst == src || memcmp(dst, src, sizeof(UChar) * src_len) == 0))
		return NULL;

	u_strToUTF8(NULL, 0, &len, dst, out_len, &status);
	if (status != U_BUFFER_OVERFLOW_ERROR)
		check_icu(status, "u_strToUTF8");
	status = U_ZERO_ERROR;
	out = palloc(len + 1);
	u_strToUTF8(out, len + 1, NULL, dst, out_len, &status);
	check_icu(status, "u_strToUTF8");
	return out;
}

PG_FUNCTION_INFO_V1(icu_normalize_init);
Datum
icu_normalize_init(PG_FUNCTION_ARGS)
{
	List	   *options = (List *) PG_GETARG_POINTER(0);

	if (options != NIL)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("unrecognized icu_normalize parameter: \"%s\"",
						((DefElem *) linitial(options))->defname)));
	PG_RETURN_POINTER(palloc0(1));
}

PG_FUNCTION_INFO_V1(icu_normalize_lexize);
Datum
icu_normalize_lexize(PG_FUNCTION_ARGS)
{
	char	   *in = (char *) PG_GETARG_POINTER(1);
	int32		len = PG_GETARG_INT32(2);
	char	   *out;
	TSLexeme   *res;

	if (GetDatabaseEncoding() != PG_UTF8)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("icu_normalize requires a UTF8 database")));

	out = normalize_token(in, len);
	if (out == NULL)
		PG_RETURN_POINTER(NULL);

	res = palloc0(sizeof(TSLexeme) * 2);
	if (*out != '\0')
	{
		res[0].lexeme = out;
		res[0].flags = TSL_FILTER;
	}
	PG_RETURN_POINTER(res);
}
