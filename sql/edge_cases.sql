SET client_min_messages = warning;
CREATE EXTENSION IF NOT EXISTS textsearch_intl CASCADE;

SELECT to_tsvector('intl_simple', '');
SELECT to_tsvector('intl_simple', ' ');
SELECT to_tsvector('intl_simple', E' \t\n\r ');
SELECT to_tsvector('intl_simple', '!!! ... ???');
SELECT to_tsvector('intl_simple', NULL::text) IS NULL AS null_in_null_out;
SELECT to_tsvector('intl_simple', 'a');
SELECT to_tsvector('intl_simple', '  leading and trailing  ');
SELECT to_tsvector('intl_simple', E'line one\nline two\ttabbed');

-- Positions count every non-blank token, including repeats
SELECT to_tsvector('intl_simple', 'one two one 東京 東京');

-- Documents past tsvector's 16383 position limit still parse to the end
SELECT length(v) AS lexemes, v @@ to_tsquery('intl_simple', 'wjjjj') AS last_word_indexed
FROM (SELECT to_tsvector('intl_simple', string_agg('w' || translate(i::text, '0123456789', 'abcdefghij'), ' ')) AS v
      FROM generate_series(0, 19999) i) s;

-- A long run of one script with no spaces
SELECT length(to_tsvector('intl_simple', repeat('東京駅前ラーメン店', 2000))) > 0 AS parsed;

-- Lexemes over 2047 bytes are dropped by to_tsvector, the rest are kept
SET client_min_messages = notice;
SELECT to_tsvector('intl_simple', 'before ' || repeat('x', 3000) || ' after');
SET client_min_messages = warning;

-- Parsing many documents in one statement reuses the cached iterator
SELECT count(*) FILTER (WHERE length(to_tsvector('intl_simple', '東京 ' || i)) = 2) AS ok
FROM generate_series(1, 10000) i;

-- An error in the same statement leaves the parser usable afterwards
SELECT to_tsvector('intl_simple', '東京駅'), 1 / 0;
SELECT to_tsvector('intl_simple', '東京駅');

-- A token that folds to combining marks alone is a stopword, not an empty lexeme
SELECT to_tsvector('intl_search', U&'\3164\0361 moreno');
SELECT to_tsvector('intl_search_ngram', U&'\3164\0361');
SELECT ts_lexize('intl_normalize', U&'\3164\0361');

-- Other encodings are rejected
SELECT current_setting('server_version_num')::int >= 150000 AS has_locale_provider \gset
\if :has_locale_provider
CREATE DATABASE textsearch_intl_latin1 ENCODING 'LATIN1' LOCALE_PROVIDER libc LC_COLLATE 'C' LC_CTYPE 'C' TEMPLATE template0;
\else
CREATE DATABASE textsearch_intl_latin1 ENCODING 'LATIN1' LC_COLLATE 'C' LC_CTYPE 'C' TEMPLATE template0;
\endif
\c textsearch_intl_latin1
SET client_min_messages = warning;
CREATE EXTENSION textsearch_intl CASCADE;
SELECT to_tsvector('intl_simple', 'abc');
\c contrib_regression
DROP DATABASE textsearch_intl_latin1;
