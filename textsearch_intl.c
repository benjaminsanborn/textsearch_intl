#include "postgres.h"

#include "fmgr.h"
#include "mb/pg_wchar.h"
#include "commands/defrem.h"
#include "nodes/parsenodes.h"
#include "nodes/pg_list.h"
#include "tsearch/ts_public.h"
#include "catalog/pg_collation.h"
#include "utils/builtins.h"
#include "utils/formatting.h"
#include "utils/memutils.h"

#include <unicode/ubrk.h>
#include <unicode/unorm2.h>
#include <unicode/uscript.h>
#include <unicode/ustring.h>
#include <unicode/utf8.h>
#include <unicode/utext.h>
#include <unicode/uversion.h>
#include <unicode/uchar.h>
#include <unicode/utrans.h>
#include <unicode/uset.h>

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
	uint32		id;
	uint32		seq;
	int32		prev_word_end;
	MemoryContextCallback cleanup;
} IcuParserState;

/*
 * The parser publishes which token is current and whether only spaces or
 * dashes separate it from the previous word, so intl_join, which runs on that
 * token before the parser is asked for the next one, can add the two words
 * run together.
 */
static uint32 parser_count = 0;
static uint32 join_parser = 0;
static uint32 join_seq = 0;
static bool join_adjacent = false;

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
	if (join_parser == state->id)
		join_adjacent = false;
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
	state->id = ++parser_count;
	state->prev_word_end = -1;
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

/*
 * Returns the byte offset of the first switch between digits and letters of a
 * spaced script (7Eleven, 3.5km), or -1.  Marks stay with the character before
 * them.
 */
static int32
digit_split(const char *s, int32 len)
{
	int32		i = 0;
	int			prev = 0;

	while (i < len)
	{
		int32		at = i;
		UChar32		c;
		int8		type;
		int			kind;

		if ((unsigned char) s[i] < 0x80)
		{
			c = s[i++];
			kind = isdigit(c) ? 1 : isalpha(c) ? 2 : 0;
			if (prev != 0 && kind != 0 && prev != kind)
				return at;
			prev = kind;
			continue;
		}
		U8_NEXT(s, i, len, c);
		if (c < 0)
			return -1;
		type = u_charType(c);
		if (type == U_NON_SPACING_MARK || type == U_COMBINING_SPACING_MARK ||
			type == U_ENCLOSING_MARK)
			continue;
		if (type == U_DECIMAL_DIGIT_NUMBER)
			kind = 1;
		else if (u_isalpha(c) && !is_unsegmented_script(c) && !is_ngram_char(c))
			kind = 2;
		else
			kind = 0;
		if (prev != 0 && kind != 0 && prev != kind)
			return at;
		prev = kind;
	}
	return -1;
}

static bool
only_spaces_or_dashes(const char *s, int32 len)
{
	int32		i = 0;

	while (i < len)
	{
		UChar32		c;

		U8_NEXT(s, i, len, c);
		if (c < 0 || (!u_isUWhiteSpace(c) && u_charType(c) != U_DASH_PUNCTUATION))
			return false;
	}
	return true;
}

static bool
is_kana(UChar32 c)
{
	UErrorCode	status = U_ZERO_ERROR;
	UScriptCode script;

	if (c == 0x30FC)
		return true;
	script = uscript_getScript(c, &status);
	return U_SUCCESS(status) &&
		(script == USCRIPT_HIRAGANA || script == USCRIPT_KATAKANA);
}

/*
 * Returns 1 for a segment that starts with Thai, Lao, Khmer or Burmese, 2 for
 * one that starts with kana, else 0.
 */
static int
run_kind(const char *s, int32 len)
{
	int32		i = 0;
	UChar32		c;

	if (len <= 0)
		return 0;
	U8_NEXT(s, i, len, c);
	if (c < 0)
		return 0;
	if (is_unsegmented_script(c))
		return 1;
	if (is_kana(c))
		return 2;
	return 0;
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
 * between them come out as one token, as do adjacent kana words, so
 * intl_ngram can match across the word boundaries ICU guessed.  ICU's
 * dictionary splits hiragana (らーめん → ら ー めん) but not katakana, so
 * without this a hiragana spelling would not match the katakana one.  Other words are split where digits meet
 * letters, so 7Eleven gives the same tokens as 7-Eleven and 7 Eleven.
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
	int			kind = 0;

	if (end == UBRK_DONE)
		PG_RETURN_INT32(0);

	type = token_type(rule);
	if (state->keep_runs && type != ICU_BLANK)
		kind = run_kind(state->buf + state->start, end - state->start);
	if (kind != 0)
	{
		for (;;)
		{
			int32		next_rule = 0;
			int32		next_end = next_segment(state, &next_rule);
			int			next_type;

			if (next_end == UBRK_DONE)
				break;
			next_type = token_type(next_rule);
			if (next_type != ICU_BLANK &&
				run_kind(state->buf + end, next_end - end) == kind)
			{
				end = next_end;
				type = kind == 1 ? ICU_WORD : ICU_KANA;
				continue;
			}
			state->has_pending = true;
			state->pending_end = next_end;
			state->pending_rule = next_rule;
			break;
		}
	}
	else if (type == ICU_WORD || type == ICU_NUMBER)
	{
		int32		split = digit_split(state->buf + state->start, end - state->start);
		UChar32		first;
		int32		i = 0;

		if (split > 0)
		{
			state->has_pending = true;
			state->pending_end = end;
			state->pending_rule = rule;
			end = state->start + split;
		}
		U8_NEXT(state->buf + state->start, i, end - state->start, first);
		type = u_charType(first) == U_DECIMAL_DIGIT_NUMBER ? ICU_NUMBER : ICU_WORD;
	}

	if (type != ICU_BLANK)
	{
		join_parser = state->id;
		join_seq = ++state->seq;
		join_adjacent = state->prev_word_end >= 0 &&
			only_spaces_or_dashes(state->buf + state->prev_word_end,
								  state->start - state->prev_word_end);
		state->prev_word_end = end;
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

static bool
is_apostrophe(UChar32 c)
{
	switch (c)
	{
		case 0x0027:
		case 0x0060:
		case 0x00B4:
		case 0x02BB:
		case 0x02BC:
		case 0x2018:
		case 0x2019:
		case 0x201B:
		case 0x2032:
		case 0xFF07:
			return true;
	}
	return false;
}

static UChar *
utf8_to_utf16(const char *in, int32 len, int32 *out_len)
{
	UErrorCode	status = U_ZERO_ERROR;
	UChar	   *out;

	u_strFromUTF8(NULL, 0, out_len, in, len, &status);
	if (status != U_BUFFER_OVERFLOW_ERROR && status != U_STRING_NOT_TERMINATED_WARNING)
		check_icu(status, "u_strFromUTF8");
	status = U_ZERO_ERROR;
	out = palloc(sizeof(UChar) * (*out_len + 1));
	u_strFromUTF8(out, *out_len + 1, NULL, in, len, &status);
	check_icu(status, "u_strFromUTF8");
	return out;
}

static char *
utf16_to_utf8(const UChar *in, int32 len)
{
	UErrorCode	status = U_ZERO_ERROR;
	int32		out_len;
	char	   *out;

	u_strToUTF8(NULL, 0, &out_len, in, len, &status);
	if (status != U_BUFFER_OVERFLOW_ERROR && status != U_STRING_NOT_TERMINATED_WARNING)
		check_icu(status, "u_strToUTF8");
	status = U_ZERO_ERROR;
	out = palloc(out_len + 1);
	u_strToUTF8(out, out_len + 1, NULL, in, len, &status);
	check_icu(status, "u_strToUTF8");
	return out;
}

/*
 * Arabic and Persian letters that people type interchangeably in names: alef
 * forms, alef maksura and Persian yeh, Persian keheh, and teh marbuta.
 * Returns 0 for tatweel and short-vowel marks, which are dropped.
 */
static UChar32
fold_arabic(UChar32 c)
{
	switch (c)
	{
		case 0x0622:
		case 0x0623:
		case 0x0625:
		case 0x0671:
			return 0x0627;
		case 0x0649:
		case 0x06CC:
			return 0x064A;
		case 0x06A9:
			return 0x0643;
		case 0x0629:
			return 0x0647;
		case 0x0640:
		case 0x0670:
			return 0;
	}
	if (c >= 0x064B && c <= 0x065F)
		return 0;
	return c;
}

typedef struct HanPair
{
	UChar32		from;
	UChar32		to;
} HanPair;

static HanPair *hant_hans = NULL;
static int32 hant_hans_count = 0;

/*
 * ICU's Traditional-Simplified transliterator maps one character at a time,
 * but running it per token is slow, so its mappings are read once into a
 * sorted table.
 */
static void
load_hant_hans(void)
{
	UErrorCode	status = U_ZERO_ERROR;
	UTransliterator *trans;
	USet	   *source;
	int32		size;
	HanPair    *pairs;
	int32		n = 0;

	trans = utrans_openU(u"Traditional-Simplified", -1, UTRANS_FORWARD, NULL, 0, NULL, &status);
	check_icu(status, "utrans_openU");
	source = utrans_getSourceSet(trans, false, NULL, &status);
	check_icu(status, "utrans_getSourceSet");
	size = uset_size(source);
	pairs = MemoryContextAlloc(TopMemoryContext, sizeof(HanPair) * (size + 1));
	for (int32 k = 0; k < size; k++)
	{
		UChar32		c = uset_charAt(source, k);
		UChar		buf[8];
		int32		len = 0;
		int32		limit;
		int32		j = 0;
		UChar32		to;

		U16_APPEND_UNSAFE(buf, len, c);
		limit = len;
		utrans_transUChars(trans, buf, &len, 8, 0, &limit, &status);
		check_icu(status, "utrans_transUChars");
		U16_NEXT(buf, j, len, to);
		if (j == len && to != c)
		{
			pairs[n].from = c;
			pairs[n].to = to;
			n++;
		}
	}
	uset_close(source);
	utrans_close(trans);
	hant_hans_count = n;
	hant_hans = pairs;
}

static UChar32
simplify_han(UChar32 c)
{
	int32		lo = 0;
	int32		hi;

	if (hant_hans == NULL)
		load_hant_hans();
	hi = hant_hans_count - 1;
	while (lo <= hi)
	{
		int32		mid = (lo + hi) / 2;

		if (hant_hans[mid].from == c)
			return hant_hans[mid].to;
		if (hant_hans[mid].from < c)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return c;
}

/*
 * Case-folds with NFKC_Casefold, which also applies NFKC and drops default
 * ignorables (soft hyphen, zero-width joiners, variation selectors), then
 * removes apostrophes, folds Arabic letter variants, maps every decimal
 * digit to ASCII, writes hiragana as katakana and traditional Chinese
 * characters as simplified.  NFKC splits the Thai and Lao vowel sara am into nikhahit +
 * sara aa, which ICU's dictionaries don't recognize, so the pair is
 * recombined.  U+0130 becomes a plain i rather than i + combining dot.
 */
static UChar *
fold_text(const UChar *src, int32 len, int32 *out_len)
{
	UErrorCode	status = U_ZERO_ERROR;
	const UNormalizer2 *nfkc_cf;
	UChar	   *pre = palloc(sizeof(UChar) * (len + 1));
	UChar	   *norm;
	UChar	   *out;
	int32		pre_len = 0;
	int32		norm_len;
	int32		i = 0;
	int32		n = 0;

	for (int32 j = 0; j < len; j++)
	{
		if (is_apostrophe(src[j]))
			continue;
		pre[pre_len++] = src[j] == 0x0130 ? 'i' : src[j];
	}

	nfkc_cf = unorm2_getNFKCCasefoldInstance(&status);
	check_icu(status, "unorm2_getNFKCCasefoldInstance");
	if (unorm2_quickCheck(nfkc_cf, pre, pre_len, &status) == UNORM_YES)
	{
		norm = pre;
		norm_len = pre_len;
	}
	else
	{
		status = U_ZERO_ERROR;
		norm_len = unorm2_normalize(nfkc_cf, pre, pre_len, NULL, 0, &status);
		if (status != U_BUFFER_OVERFLOW_ERROR)
			check_icu(status, "unorm2_normalize");
		status = U_ZERO_ERROR;
		norm = palloc(sizeof(UChar) * (norm_len + 1));
		unorm2_normalize(nfkc_cf, pre, pre_len, norm, norm_len + 1, &status);
	}
	check_icu(status, "unorm2_normalize");

	out = palloc(sizeof(UChar) * (norm_len + 1));
	while (i < norm_len)
	{
		UChar32		c;

		U16_NEXT(norm, i, norm_len, c);
		if (c == 0x0E4D && i < norm_len && norm[i] == 0x0E32)
		{
			c = 0x0E33;
			i++;
		}
		else if (c == 0x0ECD && i < norm_len && norm[i] == 0x0EB2)
		{
			c = 0x0EB3;
			i++;
		}
		else if (is_apostrophe(c))
			continue;
		else if (c > 0x7F && u_charType(c) == U_DECIMAL_DIGIT_NUMBER)
			c = '0' + u_charDigitValue(c);
		else if (c >= 0x0622 && c <= 0x06CC)
		{
			c = fold_arabic(c);
			if (c == 0)
				continue;
		}
		else if ((c >= 0x3041 && c <= 0x3096) || c == 0x309D || c == 0x309E)
			c += 0x60;
		else if (c >= 0x3400)
			c = simplify_han(c);
		U16_APPEND_UNSAFE(out, n, c);
	}
	*out_len = n;
	return out;
}

static UTransliterator *latin_ascii = NULL;

/*
 * Folds Latin accents the way unaccent does, for lexemes that intl_normalize
 * returns directly instead of passing on to unaccent.
 */
static UChar *
fold_latin(UChar *buf, int32 *len)
{
	UErrorCode	status = U_ZERO_ERROR;
	int32		capacity = *len * 4 + 16;
	UChar	   *out = palloc(sizeof(UChar) * capacity);
	int32		limit = *len;

	if (latin_ascii == NULL)
	{
		latin_ascii = utrans_openU(u"Latin-ASCII", -1, UTRANS_FORWARD, NULL, 0, NULL, &status);
		check_icu(status, "utrans_openU");
	}
	memcpy(out, buf, sizeof(UChar) * *len);
	utrans_transUChars(latin_ascii, out, len, capacity, 0, &limit, &status);
	check_icu(status, "utrans_transUChars");
	return out;
}

static const char *const elision_prefixes[] = {
	"l", "d", "j", "m", "n", "s", "t", "c", "qu", "jusqu", "lorsqu", "puisqu",
	"dell", "dall", "nell", "sull", "all", "coll", "un", NULL
};

/*
 * Returns the UTF-16 offset just past an elided French or Italian article or
 * pronoun (l', d', qu', dell' ...), or -1.  The word after it needs at least
 * two letters.
 */
static int32
elision_end(const UChar *s, int32 len)
{
	char		prefix[8];
	int32		i = 0;
	int32		letters = 0;

	while (i < len && i < 7 && s[i] < 0x80 && isalpha(s[i]))
	{
		prefix[i] = pg_ascii_tolower((char) s[i]);
		i++;
	}
	if (i == 0 || i >= len || !is_apostrophe(s[i]))
		return -1;
	prefix[i] = '\0';
	for (int32 j = i + 1; j < len; j++)
		if (u_isalpha(s[j]))
			letters++;
	if (letters < 2)
		return -1;
	for (int32 k = 0; elision_prefixes[k]; k++)
		if (strcmp(prefix, elision_prefixes[k]) == 0)
			return i + 1;
	return -1;
}

/*
 * Returns the offset just past a leading Arabic definite article (ال or وال),
 * or -1.  At least two letters have to remain.
 */
static int32
arabic_article_end(const UChar *s, int32 len)
{
	int32		start = 0;

	if (len >= 1 && s[0] == 0x0648)
		start = 1;
	if (len - start >= 4 && s[start] == 0x0627 && s[start + 1] == 0x0644)
		return start + 2;
	return -1;
}

/*
 * Returns the UTF-16 offsets where a lowercase letter is followed by an
 * uppercase one (CarrefourSA, McDonald's), ending with len, or NULL if there
 * are none.
 */
static int32 *
case_splits(const UChar *s, int32 len, int32 *count)
{
	int32	   *splits = NULL;
	int32		i = 0;
	bool		prev_lower = false;

	*count = 0;
	while (i < len)
	{
		int32		at = i;
		UChar32		c;

		U16_NEXT(s, i, len, c);
		if (prev_lower && (u_isupper(c) || u_istitle(c)))
		{
			if (splits == NULL)
				splits = palloc(sizeof(int32) * (len + 1));
			splits[(*count)++] = at;
		}
		if (u_charType(c) != U_NON_SPACING_MARK)
			prev_lower = u_islower(c);
	}
	if (splits != NULL)
		splits[(*count)++] = len;
	return splits;
}

/*
 * True when the text has a letter or digit.  NFKC_Casefold drops default
 * ignorables such as the Hangul filler, which the parser counts as a letter,
 * so a token like U+3164 U+0361 can fold to combining marks alone.  Passed on,
 * unaccent strips those to an empty lexeme, which to_tsvector rejects.
 */
static bool
has_alnum(const UChar *s, int32 len)
{
	int32		i = 0;

	while (i < len)
	{
		UChar32		c;

		U16_NEXT(s, i, len, c);
		if (u_isalnum(c))
			return true;
	}
	return false;
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

/*
 * A filtering dictionary can only pass one lexeme on, so a token with an
 * elided article (L'Occitane) or an Arabic article (المطعم) instead returns
 * two final lexemes at its position, the whole word and the word without the
 * article, already folded the way unaccent and simple would.  A query for
 * either form then matches.
 *
 * A token with a lowercase letter followed by an uppercase one (CarrefourSA)
 * likewise returns the whole word and its parts, all at its position.  The
 * parts are a separate variant, so a query for CarrefourSA matches either the
 * whole word or both parts, and one written Carrefoursa still matches.
 */
PG_FUNCTION_INFO_V1(intl_normalize_lexize);
Datum
intl_normalize_lexize(PG_FUNCTION_ARGS)
{
	char	   *in = (char *) PG_GETARG_POINTER(1);
	int32		len = PG_GETARG_INT32(2);
	UChar	   *src;
	UChar	   *folded;
	UChar	   *rest = NULL;
	int32		src_len;
	int32		folded_len;
	int32		rest_len = 0;
	int32		cut;
	bool		latin = false;
	bool		plain_ascii = true;
	bool		camel = false;
	int32	   *splits;
	int32		nsplits;
	TSLexeme   *res;

	if (GetDatabaseEncoding() != PG_UTF8)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("intl_normalize requires a UTF8 database")));

	for (int32 i = 0; i < len; i++)
	{
		if ((unsigned char) in[i] >= 0x80 || in[i] == '\'' || in[i] == '`')
			plain_ascii = false;
		else if (i > 0 && islower((unsigned char) in[i - 1]) && isupper((unsigned char) in[i]))
			camel = true;
	}
	if (plain_ascii && !camel)
		PG_RETURN_POINTER(NULL);

	src = utf8_to_utf16(in, len, &src_len);
	folded = fold_text(src, src_len, &folded_len);
	if (!has_alnum(folded, folded_len))
		PG_RETURN_POINTER(palloc0(sizeof(TSLexeme)));

	cut = elision_end(src, src_len);
	if (cut >= 0)
	{
		rest = fold_text(src + cut, src_len - cut, &rest_len);
		latin = true;
	}
	else
	{
		cut = arabic_article_end(folded, folded_len);
		if (cut >= 0)
		{
			rest = folded + cut;
			rest_len = folded_len - cut;
		}
	}

	if (rest == NULL && !starts_unsegmented(in, len) &&
		(splits = case_splits(src, src_len, &nsplits)) != NULL)
	{
		int32		from = 0;

		res = palloc0(sizeof(TSLexeme) * (nsplits + 2));
		folded = fold_latin(folded, &folded_len);
		res[0].lexeme = utf16_to_utf8(folded, folded_len);
		for (int32 j = 0; j < nsplits; j++)
		{
			int32		part_len;
			UChar	   *part = fold_text(src + from, splits[j] - from, &part_len);

			part = fold_latin(part, &part_len);
			res[j + 1].lexeme = utf16_to_utf8(part, part_len);
			res[j + 1].nvariant = 1;
			from = splits[j];
		}
		PG_RETURN_POINTER(res);
	}

	if (rest != NULL)
	{
		if (latin)
		{
			folded = fold_latin(folded, &folded_len);
			rest = fold_latin(rest, &rest_len);
		}
		res = palloc0(sizeof(TSLexeme) * 3);
		res[0].lexeme = utf16_to_utf8(folded, folded_len);
		res[1].lexeme = utf16_to_utf8(rest, rest_len);
		PG_RETURN_POINTER(res);
	}

	if (folded_len == src_len && memcmp(folded, src, sizeof(UChar) * src_len) == 0)
		PG_RETURN_POINTER(NULL);

	res = palloc0(sizeof(TSLexeme) * 2);
	if (folded_len > 0)
	{
		res[0].lexeme = utf16_to_utf8(folded, folded_len);
		res[0].flags = TSL_FILTER;
	}
	PG_RETURN_POINTER(res);
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

typedef struct JoinDict
{
	int32		max_length;
} JoinDict;

static uint32 last_parser = 0;
static uint32 last_seq = 0;
static char last_word[64];
static int32 last_chars = -1;

/*
 * Returns the number of characters in a word of a spaced script, or -1 for a
 * word with Han, kana, Hangul, Thai, Lao, Khmer or Burmese in it.
 */
static int32
spaced_chars(const char *s, int32 len)
{
	int32		i = 0;
	int32		chars = 0;

	while (i < len)
	{
		UChar32		c;

		chars++;
		if ((unsigned char) s[i] < 0x80)
		{
			i++;
			continue;
		}
		U8_NEXT(s, i, len, c);
		if (c < 0 || is_ngram_char(c) || is_unsegmented_script(c))
			return -1;
	}
	return chars;
}

PG_FUNCTION_INFO_V1(intl_join_init);
Datum
intl_join_init(PG_FUNCTION_ARGS)
{
	List	   *options = (List *) PG_GETARG_POINTER(0);
	JoinDict   *d = palloc0(sizeof(JoinDict));
	ListCell   *l;

	d->max_length = 0;
	foreach(l, options)
	{
		DefElem    *def = (DefElem *) lfirst(l);

		if (strcmp(def->defname, "max_length") == 0)
			d->max_length = atoi(defGetString(def));
		else
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("unrecognized intl_join parameter: \"%s\"", def->defname)));
	}
	PG_RETURN_POINTER(d);
}

/*
 * Lowercases the token like simple.  With max_length set, when the previous
 * token was also lexized here, only spaces or dashes separate the two, and one
 * of them has at most max_length characters, also adds both words run
 * together (mc donalds → mcdonalds, Sài Gòn → saigon) as a second variant at
 * this token's position, so a phrase query for one spelling finds the other.
 * Words in scripts written without spaces are never joined.
 */
PG_FUNCTION_INFO_V1(intl_join_lexize);
Datum
intl_join_lexize(PG_FUNCTION_ARGS)
{
	JoinDict   *d = (JoinDict *) PG_GETARG_POINTER(0);
	char	   *in = (char *) PG_GETARG_POINTER(1);
	int32		len = PG_GETARG_INT32(2);
	char	   *word = str_tolower(in, len, DEFAULT_COLLATION_OID);
	int32		word_len = strlen(word);
	TSLexeme   *res = palloc0(sizeof(TSLexeme) * 3);
	int32		chars = word_len < (int32) sizeof(last_word) ? spaced_chars(word, word_len) : -1;

	res[0].lexeme = word;
	if (chars > 0 && last_chars > 0 && join_adjacent &&
		last_parser == join_parser && last_seq + 1 == join_seq &&
		Min(chars, last_chars) <= d->max_length)
	{
		res[1].lexeme = psprintf("%s%s", last_word, word);
		res[1].nvariant = 1;
	}
	last_chars = chars;
	if (chars > 0)
		memcpy(last_word, word, word_len + 1);
	last_parser = join_parser;
	last_seq = join_seq;
	PG_RETURN_POINTER(res);
}
