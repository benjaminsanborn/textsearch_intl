SET client_min_messages = warning;
CREATE EXTENSION IF NOT EXISTS icu_parser;

SELECT tokid, alias, description FROM ts_token_type('icu') ORDER BY tokid;

SELECT icu_parser_icu_version() ~ '^[0-9]+\.[0-9]+' AS has_icu_version;

SELECT alias, dictionaries FROM ts_debug('icu_simple', 'a 1 カナ 漢字') WHERE alias <> 'blank';

-- Stemming and stop words for Latin text, ICU segmentation for the rest
CREATE TEXT SEARCH CONFIGURATION icu_english (PARSER = icu);
ALTER TEXT SEARCH CONFIGURATION icu_english ADD MAPPING FOR word WITH english_stem;
ALTER TEXT SEARCH CONFIGURATION icu_english ADD MAPPING FOR number, kana, ideo WITH simple;
SELECT to_tsvector('icu_english', 'The running cafés of 東京駅前');

-- Accent folding through unaccent
CREATE EXTENSION IF NOT EXISTS unaccent;
CREATE TEXT SEARCH CONFIGURATION icu_unaccent (PARSER = icu);
ALTER TEXT SEARCH CONFIGURATION icu_unaccent ADD MAPPING FOR word, number, kana, ideo WITH unaccent, simple;
SELECT to_tsvector('icu_unaccent', 'Café Zürich São Paulo');
SELECT to_tsvector('icu_unaccent', 'Café') @@ plainto_tsquery('icu_unaccent', 'cafe') AS folded;

-- Leaving a token type unmapped drops it
ALTER TEXT SEARCH CONFIGURATION icu_english DROP MAPPING FOR number;
SELECT to_tsvector('icu_english', 'route 66 東京');

-- Unknown token types are rejected
ALTER TEXT SEARCH CONFIGURATION icu_english ADD MAPPING FOR asciiword WITH simple;

DROP TEXT SEARCH CONFIGURATION icu_english;
DROP TEXT SEARCH CONFIGURATION icu_unaccent;

-- The extension can move schemas and be dropped and recreated
CREATE SCHEMA moved;
ALTER EXTENSION icu_parser SET SCHEMA moved;
SELECT to_tsvector('moved.icu_simple', '東京駅');
ALTER EXTENSION icu_parser SET SCHEMA public;
DROP EXTENSION icu_parser;
DROP SCHEMA moved;
CREATE EXTENSION icu_parser;
SELECT to_tsvector('icu_simple', '東京駅');
