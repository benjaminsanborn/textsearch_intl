SET client_min_messages = warning;
CREATE EXTENSION IF NOT EXISTS icu_parser;

SELECT to_tsvector('icu_simple', '');
SELECT to_tsvector('icu_simple', ' ');
SELECT to_tsvector('icu_simple', E' \t\n\r ');
SELECT to_tsvector('icu_simple', '!!! ... ???');
SELECT to_tsvector('icu_simple', NULL::text) IS NULL AS null_in_null_out;
SELECT to_tsvector('icu_simple', 'a');
SELECT to_tsvector('icu_simple', '  leading and trailing  ');
SELECT to_tsvector('icu_simple', E'line one\nline two\ttabbed');

-- Positions count every non-blank token, including repeats
SELECT to_tsvector('icu_simple', 'one two one 東京 東京');

-- Documents past tsvector's 16383 position limit still parse to the end
SELECT length(v) AS lexemes, v @@ to_tsquery('icu_simple', 'w19999') AS last_word_indexed
FROM (SELECT to_tsvector('icu_simple', string_agg('w' || i, ' ')) AS v
      FROM generate_series(0, 19999) i) s;

-- A long run of one script with no spaces
SELECT length(to_tsvector('icu_simple', repeat('東京駅前ラーメン店', 2000))) > 0 AS parsed;

-- Lexemes over 2047 bytes are dropped by to_tsvector, the rest are kept
SET client_min_messages = notice;
SELECT to_tsvector('icu_simple', 'before ' || repeat('x', 3000) || ' after');
SET client_min_messages = warning;

-- Parsing many documents in one statement reuses the cached iterator
SELECT count(*) FILTER (WHERE length(to_tsvector('icu_simple', '東京 ' || i)) = 2) AS ok
FROM generate_series(1, 10000) i;

-- An error in the same statement leaves the parser usable afterwards
SELECT to_tsvector('icu_simple', '東京駅'), 1 / 0;
SELECT to_tsvector('icu_simple', '東京駅');

-- Other encodings are rejected
SELECT current_setting('server_version_num')::int >= 150000 AS has_locale_provider \gset
\if :has_locale_provider
CREATE DATABASE icu_parser_latin1 ENCODING 'LATIN1' LOCALE_PROVIDER libc LC_COLLATE 'C' LC_CTYPE 'C' TEMPLATE template0;
\else
CREATE DATABASE icu_parser_latin1 ENCODING 'LATIN1' LC_COLLATE 'C' LC_CTYPE 'C' TEMPLATE template0;
\endif
\c icu_parser_latin1
SET client_min_messages = warning;
CREATE EXTENSION icu_parser;
SELECT to_tsvector('icu_simple', 'abc');
\c contrib_regression
DROP DATABASE icu_parser_latin1;
