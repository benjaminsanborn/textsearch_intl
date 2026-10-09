SET client_min_messages = warning;
CREATE EXTENSION IF NOT EXISTS icu_parser CASCADE;
RESET client_min_messages;

SELECT alias, description FROM ts_token_type('icu') ORDER BY tokid;

SELECT alias, token, lexemes FROM ts_debug('icu_simple', '東京駅前ラーメン店 ร้านกาแฟ Café McDonald''s 7-Eleven 3.5km');

SELECT to_tsvector('icu_simple', 'Starbucks Coffee 星巴克咖啡 สตาร์บัคส์');

SELECT to_tsvector('icu_simple', '東京駅前ラーメン店') @@ plainto_tsquery('icu_simple', 'ラーメン');
SELECT to_tsvector('icu_simple', 'ร้านกาแฟสด') @@ plainto_tsquery('icu_simple', 'กาแฟ');
SELECT to_tsvector('icu_simple', 'CAFÉ Ünïcode') @@ phraseto_tsquery('icu_simple', 'café ünïcode');

SELECT to_tsvector('icu_simple', '');
SELECT to_tsvector('icu_simple', '   !!! ');

SELECT ts_headline('icu_simple', 'The best ラーメン in Tokyo', plainto_tsquery('icu_simple', 'ラーメン'));

CREATE TABLE icu_names (name text);
INSERT INTO icu_names VALUES ('東京駅前ラーメン店'), ('Blue Bottle Coffee'), (NULL);
CREATE INDEX ON icu_names USING gin (to_tsvector('icu_simple', name));
SET enable_seqscan = off;
SELECT name FROM icu_names WHERE to_tsvector('icu_simple', name) @@ plainto_tsquery('icu_simple', 'ラーメン');

RESET enable_seqscan;
DROP TABLE icu_names;
