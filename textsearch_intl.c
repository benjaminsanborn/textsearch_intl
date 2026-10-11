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
#include <unicode/uscript.h>
#include <unicode/ustring.h>
#include <unicode/utf8.h>
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
	bool		keep_runs;
	bool		has_pending;
	int32		pending_end;
	int32		pending_rule;
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
				(errmsg("textsearch_intl: %s failed: %s", what, u_errorName(status))));
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

static IcuParserState *
start_parser(char *buf, int32 len, bool keep_runs)
{
	IcuParserState *state;
	UErrorCode	status = U_ZERO_ERROR;

	if (GetDatabaseEncoding() != PG_UTF8)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("textsearch_intl requires a UTF8 database")));

	if (shared_iter == NULL)
	{
		shared_iter = ubrk_open(UBRK_WORD, "", NULL, 0, &status);
		check_icu(status, "ubrk_open");
	}

	state = palloc0(sizeof(IcuParserState));
	state->buf = buf;
	state->keep_runs = keep_runs;
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
	return state;
}

PG_FUNCTION_INFO_V1(intl_prsstart);
Datum
intl_prsstart(PG_FUNCTION_ARGS)
{
	PG_RETURN_POINTER(start_parser((char *) PG_GETARG_POINTER(0),
								   PG_GETARG_INT32(1), false));
}

PG_FUNCTION_INFO_V1(intl_runs_prsstart);
Datum
intl_runs_prsstart(PG_FUNCTION_ARGS)
{
	PG_RETURN_POINTER(start_parser((char *) PG_GETARG_POINTER(0),
								   PG_GETARG_INT32(1), true));
}

static bool
is_unsegmented_script(UChar32 c)
{
	UErrorCode	status = U_ZERO_ERROR;
	UScriptCode script = uscript_getScript(c, &status);

	if (U_FAILURE(status))
		return false;
	return script == USCRIPT_THAI || script == USCRIPT_LAO ||
		script == USCRIPT_KHMER || script == USCRIPT_MYANMAR;
}

static bool
starts_unsegmented(const char *s, int32 len)
{
	int32		i = 0;
	UChar32		c;

	if (len <= 0)
		return false;
	U8_NEXT(s, i, len, c);
	return c >= 0 && is_unsegmented_script(c);
}

static int
token_type(int32 rule)
{
	if (rule < UBRK_WORD_NONE_LIMIT)
		return ICU_BLANK;
	if (rule < UBRK_WORD_NUMBER_LIMIT)
		return ICU_NUMBER;
	if (rule < UBRK_WORD_LETTER_LIMIT)
		return ICU_WORD;
	if (rule < UBRK_WORD_KANA_LIMIT)
		return ICU_KANA;
	if (rule < UBRK_WORD_IDEO_LIMIT)
		return ICU_IDEO;
	return ICU_BLANK;
}

static int32
next_segment(IcuParserState *state, int32 *rule)
{
	int32		end;

	if (state->has_pending)
	{
		state->has_pending = false;
		*rule = state->pending_rule;
		return state->pending_end;
	}
	end = ubrk_next(state->iter);
	if (end != UBRK_DONE)
		*rule = ubrk_getRuleStatus(state->iter);
	return end;
}

/*
 * In keep_runs mode, adjacent Thai, Lao, Khmer and Burmese words with nothing
 * between them come out as one token, so intl_ngram can match across the
 * word boundaries ICU guessed.
 */
PG_FUNCTION_INFO_V1(intl_prsgettoken);
Datum
intl_prsgettoken(PG_FUNCTION_ARGS)
{
	IcuParserState *state = (IcuParserState *) PG_GETARG_POINTER(0);
	char	  **token = (char **) PG_GETARG_POINTER(1);
	int		   *token_len = (int *) PG_GETARG_POINTER(2);
	int32		rule = 0;
	int32		end = next_segment(state, &rule);
	int			type;

	if (end == UBRK_DONE)
		PG_RETURN_INT32(0);

	type = token_type(rule);
	if (state->keep_runs && (type == ICU_WORD || type == ICU_NUMBER) &&
		starts_unsegmented(state->buf + state->start, end - state->start))
	{
		for (;;)
		{
			int32		next_rule = 0;
			int32		next_end = next_segment(state, &next_rule);
			int			next_type;

			if (next_end == UBRK_DONE)
				break;
			next_type = token_type(next_rule);
			if ((next_type == ICU_WORD || next_type == ICU_NUMBER) &&
				starts_unsegmented(state->buf + end, next_end - end))
			{
				end = next_end;
				type = ICU_WORD;
				continue;
			}
			state->has_pending = true;
			state->pending_end = next_end;
			state->pending_rule = next_rule;
			break;
		}
	}

	*token = state->buf + state->start;
	*token_len = end - state->start;
	state->start = end;
	PG_RETURN_INT32(type);
}

PG_FUNCTION_INFO_V1(intl_prsend);
Datum
intl_prsend(PG_FUNCTION_ARGS)
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

PG_FUNCTION_INFO_V1(intl_prslextype);
Datum
intl_prslextype(PG_FUNCTION_ARGS)
{
	LexDescr   *descr = palloc0(sizeof(LexDescr) * 6);

	set_lexdescr(&descr[0], ICU_WORD, "word", "Word, letters");
	set_lexdescr(&descr[1], ICU_BLANK, "blank", "Space, punctuation and symbols");
	set_lexdescr(&descr[2], ICU_NUMBER, "number", "Number");
	set_lexdescr(&descr[3], ICU_KANA, "kana", "Kana word");
	set_lexdescr(&descr[4], ICU_IDEO, "ideo", "Ideographic word");

	PG_RETURN_POINTER(descr);
}

PG_FUNCTION_INFO_V1(intl_icu_version);
Datum
intl_icu_version(PG_FUNCTION_ARGS)
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

PG_FUNCTION_INFO_V1(intl_normalize_init);
Datum
intl_normalize_init(PG_FUNCTION_ARGS)
{
	List	   *options = (List *) PG_GETARG_POINTER(0);

	if (options != NIL)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("unrecognized intl_normalize parameter: \"%s\"",
						((DefElem *) linitial(options))->defname)));
	PG_RETURN_POINTER(palloc0(1));
}

PG_FUNCTION_INFO_V1(intl_normalize_lexize);
Datum
intl_normalize_lexize(PG_FUNCTION_ARGS)
{
	char	   *in = (char *) PG_GETARG_POINTER(1);
	int32		len = PG_GETARG_INT32(2);
	char	   *out;
	TSLexeme   *res;

	if (GetDatabaseEncoding() != PG_UTF8)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("intl_normalize requires a UTF8 database")));

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

/*
 * Characters whose words ICU keeps as long dictionary or unknown runs, so a
 * search for part of one has to match pieces of the token.  U+30FC (prolonged
 * sound mark) is Common script but only appears inside kana words.
 */
static bool
is_ngram_char(UChar32 c)
{
	UErrorCode	status = U_ZERO_ERROR;
	UScriptCode script;

	if (c == 0x30FC)
		return true;
	script = uscript_getScript(c, &status);
	if (U_FAILURE(status))
		return false;
	return script == USCRIPT_HAN || script == USCRIPT_HIRAGANA ||
		script == USCRIPT_KATAKANA || script == USCRIPT_HANGUL;
}

static UBreakIterator *char_iter = NULL;

/*
 * Thai, Lao, Khmer and Burmese tokens become bigrams and trigrams of grapheme
 * clusters, so vowel and tone marks stay with their consonant.  Bigrams let a
 * two-cluster query match inside a longer run.  Returns NULL when the token
 * has other scripts in it or is a single cluster.
 */
static TSLexeme *
cluster_trigrams(const char *in, int32 len)
{
	UErrorCode	status = U_ZERO_ERROR;
	UText		text = UTEXT_INITIALIZER;
	int32	   *starts;
	int32		n = 0;
	int32		i = 0;
	int32		pos;
	TSLexeme   *res;

	while (i < len)
	{
		UChar32		c;
		UScriptCode script;

		U8_NEXT(in, i, len, c);
		if (c < 0)
			return NULL;
		script = uscript_getScript(c, &status);
		if (U_FAILURE(status))
			return NULL;
		if (script != USCRIPT_INHERITED && !is_unsegmented_script(c))
			return NULL;
	}

	if (char_iter == NULL)
	{
		char_iter = ubrk_open(UBRK_CHARACTER, "", NULL, 0, &status);
		check_icu(status, "ubrk_open");
	}
	utext_openUTF8(&text, in, len, &status);
	check_icu(status, "utext_openUTF8");
	ubrk_setUText(char_iter, &text, &status);
	check_icu(status, "ubrk_setUText");

	starts = palloc(sizeof(int32) * (len + 1));
	for (pos = ubrk_first(char_iter); pos != UBRK_DONE; pos = ubrk_next(char_iter))
		starts[n++] = pos;
	utext_close(&text);

	n--;
	if (n < 2)
		return NULL;

	res = palloc0(sizeof(TSLexeme) * (2 * n - 2));
	pos = 0;
	for (int32 j = 0; j + 1 < n; j++)
		res[pos++].lexeme = pnstrdup(in + starts[j], starts[j + 2] - starts[j]);
	for (int32 j = 0; j + 2 < n; j++)
		res[pos++].lexeme = pnstrdup(in + starts[j], starts[j + 3] - starts[j]);
	return res;
}

PG_FUNCTION_INFO_V1(intl_ngram_init);
Datum
intl_ngram_init(PG_FUNCTION_ARGS)
{
	List	   *options = (List *) PG_GETARG_POINTER(0);

	if (options != NIL)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("unrecognized intl_ngram parameter: \"%s\"",
						((DefElem *) linitial(options))->defname)));
	PG_RETURN_POINTER(palloc0(1));
}

/*
 * Returns every character and every pair of adjacent characters of a Han,
 * kana or Hangul token, or every three adjacent grapheme clusters of a Thai,
 * Lao, Khmer or Burmese token, all at the token's position.  A query token goes
 * through the same expansion, and to_tsquery ANDs lexemes that share a
 * position, so a query matches any document token containing its pieces,
 * while phrase operators still order whole tokens.
 */
PG_FUNCTION_INFO_V1(intl_ngram_lexize);
Datum
intl_ngram_lexize(PG_FUNCTION_ARGS)
{
	char	   *in = (char *) PG_GETARG_POINTER(1);
	int32		len = PG_GETARG_INT32(2);
	int32	   *starts = palloc(sizeof(int32) * (len + 1));
	int32		n = 0;
	int32		i = 0;
	int32		k = 0;
	TSLexeme   *res;

	if (GetDatabaseEncoding() != PG_UTF8)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("intl_ngram requires a UTF8 database")));

	if (starts_unsegmented(in, len))
		PG_RETURN_POINTER(cluster_trigrams(in, len));

	while (i < len)
	{
		UChar32		c;

		starts[n++] = i;
		U8_NEXT(in, i, len, c);
		if (c < 0 || !is_ngram_char(c))
			PG_RETURN_POINTER(NULL);
	}
	starts[n] = len;
	if (n < 2)
		PG_RETURN_POINTER(NULL);

	res = palloc0(sizeof(TSLexeme) * (2 * n));
	for (int32 j = 0; j < n; j++)
		res[k++].lexeme = pnstrdup(in + starts[j], starts[j + 1] - starts[j]);
	for (int32 j = 0; j + 1 < n; j++)
		res[k++].lexeme = pnstrdup(in + starts[j], starts[j + 2] - starts[j]);
	PG_RETURN_POINTER(res);
}
