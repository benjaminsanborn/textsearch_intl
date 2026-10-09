#include "postgres.h"

#include "fmgr.h"
#include "mb/pg_wchar.h"
#include "tsearch/ts_public.h"
#include "utils/memutils.h"

#include <unicode/ubrk.h>
#include <unicode/utext.h>
#include <unicode/uversion.h>

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
	MemoryContextCallback cleanup;
} IcuParserState;

static UBreakIterator *template_iter = NULL;

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

	if (state->iter)
		ubrk_close(state->iter);
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

	if (template_iter == NULL)
	{
		template_iter = ubrk_open(UBRK_WORD, "", NULL, 0, &status);
		check_icu(status, "ubrk_open");
	}

	state = palloc0(sizeof(IcuParserState));
	state->buf = buf;
	state->cleanup.func = close_icu;
	state->cleanup.arg = state;
	MemoryContextRegisterResetCallback(CurrentMemoryContext, &state->cleanup);

	state->text = utext_openUTF8(NULL, buf, len, &status);
	check_icu(status, "utext_openUTF8");
#if U_ICU_VERSION_MAJOR_NUM >= 69
	state->iter = ubrk_clone(template_iter, &status);
#else
	state->iter = ubrk_safeClone(template_iter, NULL, NULL, &status);
#endif
	check_icu(status, "ubrk_clone");
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
