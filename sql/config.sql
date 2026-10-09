SET client_min_messages = warning;
CREATE EXTENSION IF NOT EXISTS textsearch_intl CASCADE;

SELECT tokid, alias, description FROM ts_token_type('intl') ORDER BY tokid;

SELECT intl_icu_version() ~ '^[0-9]+\.[0-9]+' AS has_icu_version;

SELECT alias, dictionaries FROM ts_debug('intl_simple', 'a 1 カナ 漢字') WHERE alias <> 'blank';

-- Stemming and stop words for Latin text, ICU segmentation for the rest
CREATE TEXT SEARCH CONFIGURATION intl_english (PARSER = intl);
ALTER TEXT SEARCH CONFIGURATION intl_english ADD MAPPING FOR word WITH english_stem;
ALTER TEXT SEARCH CONFIGURATION intl_english ADD MAPPING FOR number, kana, ideo WITH simple;
SELECT to_tsvector('intl_english', 'The running cafés of 東京駅前');

-- Accent folding through unaccent
CREATE EXTENSION IF NOT EXISTS unaccent;
CREATE TEXT SEARCH CONFIGURATION intl_unaccent (PARSER = intl);
ALTER TEXT SEARCH CONFIGURATION intl_unaccent ADD MAPPING FOR word, number, kana, ideo WITH unaccent, simple;
SELECT to_tsvector('intl_unaccent', 'Café Zürich São Paulo');
SELECT to_tsvector('intl_unaccent', 'Café') @@ plainto_tsquery('intl_unaccent', 'cafe') AS folded;

-- Leaving a token type unmapped drops it
ALTER TEXT SEARCH CONFIGURATION intl_english DROP MAPPING FOR number;
SELECT to_tsvector('intl_english', 'route 66 東京');

-- Unknown token types are rejected
ALTER TEXT SEARCH CONFIGURATION intl_english ADD MAPPING FOR asciiword WITH simple;

DROP TEXT SEARCH CONFIGURATION intl_english;
DROP TEXT SEARCH CONFIGURATION intl_unaccent;

-- The extension can move schemas and be dropped and recreated
CREATE SCHEMA moved;
ALTER EXTENSION textsearch_intl SET SCHEMA moved;
SELECT to_tsvector('moved.intl_simple', '東京駅');
ALTER EXTENSION textsearch_intl SET SCHEMA public;
DROP EXTENSION textsearch_intl;
DROP SCHEMA moved;
CREATE EXTENSION textsearch_intl CASCADE;
SELECT to_tsvector('intl_simple', '東京駅');
