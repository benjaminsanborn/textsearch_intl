\echo Use "CREATE EXTENSION textsearch_intl" to load this file. \quit

CREATE FUNCTION intl_prsstart(internal, int4)
RETURNS internal
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE FUNCTION intl_prsgettoken(internal, internal, internal)
RETURNS internal
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE FUNCTION intl_prsend(internal)
RETURNS void
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE FUNCTION intl_prslextype(internal)
RETURNS internal
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE FUNCTION intl_icu_version()
RETURNS text
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT STABLE PARALLEL SAFE;

CREATE TEXT SEARCH PARSER intl (
  START = intl_prsstart,
  GETTOKEN = intl_prsgettoken,
  END = intl_prsend,
  LEXTYPES = intl_prslextype,
  HEADLINE = pg_catalog.prsd_headline
);

CREATE TEXT SEARCH CONFIGURATION intl_simple (PARSER = intl);

ALTER TEXT SEARCH CONFIGURATION intl_simple
  ADD MAPPING FOR word, number, kana, ideo WITH simple;

CREATE FUNCTION intl_normalize_init(internal)
RETURNS internal
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE FUNCTION intl_normalize_lexize(internal, internal, internal, internal)
RETURNS internal
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE TEXT SEARCH TEMPLATE intl_normalize_template (
  INIT = intl_normalize_init,
  LEXIZE = intl_normalize_lexize
);

CREATE TEXT SEARCH DICTIONARY intl_normalize (TEMPLATE = intl_normalize_template);

CREATE TEXT SEARCH CONFIGURATION intl_search (PARSER = intl);

ALTER TEXT SEARCH CONFIGURATION intl_search
  ADD MAPPING FOR word, number, kana, ideo WITH intl_normalize, unaccent, simple;

CREATE FUNCTION intl_ngram_init(internal)
RETURNS internal
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE FUNCTION intl_ngram_lexize(internal, internal, internal, internal)
RETURNS internal
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE TEXT SEARCH TEMPLATE intl_ngram_template (
  INIT = intl_ngram_init,
  LEXIZE = intl_ngram_lexize
);

CREATE TEXT SEARCH DICTIONARY intl_ngram (TEMPLATE = intl_ngram_template);

CREATE TEXT SEARCH CONFIGURATION intl_search_ngram (PARSER = intl);

ALTER TEXT SEARCH CONFIGURATION intl_search_ngram
  ADD MAPPING FOR word, number, kana, ideo WITH intl_normalize, intl_ngram, unaccent, simple;
