\echo Use "CREATE EXTENSION icu_parser" to load this file. \quit

CREATE FUNCTION icu_prsstart(internal, int4)
RETURNS internal
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE FUNCTION icu_prsgettoken(internal, internal, internal)
RETURNS internal
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE FUNCTION icu_prsend(internal)
RETURNS void
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE FUNCTION icu_prslextype(internal)
RETURNS internal
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE TEXT SEARCH PARSER icu (
  START = icu_prsstart,
  GETTOKEN = icu_prsgettoken,
  END = icu_prsend,
  LEXTYPES = icu_prslextype,
  HEADLINE = pg_catalog.prsd_headline
);

CREATE TEXT SEARCH CONFIGURATION icu_simple (PARSER = icu);

ALTER TEXT SEARCH CONFIGURATION icu_simple
  ADD MAPPING FOR word, number, kana, ideo WITH simple;
