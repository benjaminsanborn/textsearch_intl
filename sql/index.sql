SET client_min_messages = warning;
CREATE EXTENSION IF NOT EXISTS icu_parser CASCADE;

CREATE TABLE places (
  id int PRIMARY KEY,
  name text,
  name_tsv tsvector GENERATED ALWAYS AS (to_tsvector('icu_simple', name)) STORED
);
INSERT INTO places (id, name)
SELECT i, CASE i % 4
  WHEN 0 THEN '東京駅前ラーメン店 ' || i
  WHEN 1 THEN 'ร้านกาแฟสด ' || i
  WHEN 2 THEN '北京大学图书馆 ' || i
  ELSE 'Blue Bottle Coffee ' || i
END
FROM generate_series(1, 2000) i;
ANALYZE places;

CREATE INDEX places_name_gin ON places USING gin (to_tsvector('icu_simple', name));
CREATE INDEX places_tsv_gist ON places USING gist (name_tsv);

SET enable_seqscan = off;

EXPLAIN (COSTS OFF)
SELECT count(*) FROM places WHERE to_tsvector('icu_simple', name) @@ plainto_tsquery('icu_simple', 'ラーメン');
SELECT count(*) FROM places WHERE to_tsvector('icu_simple', name) @@ plainto_tsquery('icu_simple', 'ラーメン');
SELECT count(*) FROM places WHERE to_tsvector('icu_simple', name) @@ plainto_tsquery('icu_simple', 'กาแฟ');
SELECT count(*) FROM places WHERE to_tsvector('icu_simple', name) @@ phraseto_tsquery('icu_simple', '图书馆');

SET enable_bitmapscan = off;
SET enable_indexscan = on;
DROP INDEX places_name_gin;
EXPLAIN (COSTS OFF)
SELECT count(*) FROM places WHERE name_tsv @@ plainto_tsquery('icu_simple', 'coffee');
SELECT count(*) FROM places WHERE name_tsv @@ plainto_tsquery('icu_simple', 'coffee');
RESET enable_bitmapscan;
RESET enable_seqscan;

-- The stored column and a fresh parse agree on every row
SELECT count(*) FROM places WHERE name_tsv <> to_tsvector('icu_simple', name);

-- Parallel workers each load the parser
SET parallel_setup_cost = 0;
SET parallel_tuple_cost = 0;
SET min_parallel_table_scan_size = 0;
SET max_parallel_workers_per_gather = 2;
SET enable_indexscan = off;
SET enable_bitmapscan = off;
EXPLAIN (COSTS OFF)
SELECT count(*) FROM places WHERE to_tsvector('icu_simple', name) @@ plainto_tsquery('icu_simple', 'ラーメン');
SELECT count(*) FROM places WHERE to_tsvector('icu_simple', name) @@ plainto_tsquery('icu_simple', 'ラーメン');
SELECT sum(length(to_tsvector('icu_simple', name))) FROM places;

DROP TABLE places;
