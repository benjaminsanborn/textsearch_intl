SET jit = off;
SET max_parallel_workers_per_gather = 0;
SET max_parallel_maintenance_workers = 0;
SET maintenance_work_mem = '1GB';
SET work_mem = '256MB';
SET client_min_messages = warning;

CREATE TABLE langs AS
SELECT lang, ord FROM unnest(string_to_array(:'langs', ' ')) WITH ORDINALITY AS l(lang, ord);

CREATE TABLE methods (method text PRIMARY KEY, ord int, expr text, index_def text, predicate text);
INSERT INTO methods VALUES
  ('scan only', 0, 'length(title)', NULL, NULL),
  ('default parser', 1, 'length(to_tsvector(''simple'', title))',
   'gin (to_tsvector(''simple'', title))', 'to_tsvector(''simple'', title) @@ plainto_tsquery(''simple'', %L)'),
  ('icu_parser', 2, 'length(to_tsvector(''icu_simple'', title))',
   'gin (to_tsvector(''icu_simple'', title))', 'to_tsvector(''icu_simple'', title) @@ plainto_tsquery(''icu_simple'', %L)'),
  ('pg_trgm', 3, 'cardinality(show_trgm(title))', 'gin (title gin_trgm_ops)', 'title LIKE %L');
INSERT INTO methods
SELECT 'pg_bigm', 4, 'cardinality(show_bigm(title))', 'gin (title gin_bigm_ops)', 'title LIKE %L'
WHERE EXISTS (SELECT 1 FROM pg_extension WHERE extname = 'pg_bigm');

CREATE TABLE results (lang text, method text, metric text, value numeric, PRIMARY KEY (lang, method, metric));

CREATE FUNCTION median_ms(stmt text, runs int) RETURNS numeric
LANGUAGE plpgsql AS $$
DECLARE
  started timestamptz;
  timings numeric[] := '{}';
  ignored text;
BEGIN
  EXECUTE stmt INTO ignored;
  FOR i IN 1..runs LOOP
    started := clock_timestamp();
    EXECUTE stmt INTO ignored;
    timings := timings || (extract(epoch FROM clock_timestamp() - started) * 1000)::numeric;
  END LOOP;
  RETURN (SELECT percentile_cont(0.5) WITHIN GROUP (ORDER BY t) FROM unnest(timings) t);
END
$$;

CREATE FUNCTION elapsed_ms(stmt text) RETURNS numeric
LANGUAGE plpgsql AS $$
DECLARE
  started timestamptz := clock_timestamp();
BEGIN
  EXECUTE stmt;
  RETURN (extract(epoch FROM clock_timestamp() - started) * 1000)::numeric;
END
$$;

CREATE FUNCTION query_arg(method text, term text) RETURNS text
LANGUAGE sql IMMUTABLE AS $$
  SELECT CASE WHEN method LIKE 'pg\_%'
    THEN '%' || replace(replace(replace(term, '\', '\\'), '%', '\%'), '_', '\_') || '%'
    ELSE term END
$$;

-- Runs every query term for a language through one method's own table and index, returning total matches
CREATE FUNCTION run_terms(p_lang text, p_method text) RETURNS bigint
LANGUAGE plpgsql AS $$
DECLARE
  pred text := (SELECT predicate FROM methods WHERE method = p_method);
  t text;
  n bigint;
  total bigint := 0;
BEGIN
  FOR t IN SELECT term FROM terms WHERE lang = p_lang ORDER BY term LOOP
    IF pred IS NULL THEN
      EXECUTE format('SELECT count(*) FROM docs_%s WHERE strpos(title, $1) > 0', p_lang) INTO n USING t;
    ELSE
      EXECUTE format('SELECT count(*) FROM docs_%s_%s WHERE ' || pred, p_lang,
                     (SELECT ord FROM methods WHERE method = p_method), query_arg(p_method, t)) INTO n;
    END IF;
    total := total + n;
  END LOOP;
  RETURN total;
END
$$;

-- Deterministic split per language: 50,000 held-out titles for query terms, then the sample
CREATE TABLE ranked AS
SELECT lang, title, row_number() OVER (PARTITION BY lang ORDER BY md5(title)) AS rn
FROM titles_raw;
CREATE TABLE heldout AS SELECT lang, title FROM ranked WHERE rn <= 50000;
CREATE TABLE docs AS SELECT lang, title FROM ranked WHERE rn > 50000 AND rn <= 50000 + :sample;
DROP TABLE ranked;

DROP TABLE titles_raw;

-- 200 query terms per language: ICU words of two or more characters, frequency ranks 101-300
CREATE TABLE terms AS
SELECT l.lang, w.word AS term
FROM langs l
CROSS JOIN LATERAL (
  SELECT word FROM ts_stat(format(
    'SELECT to_tsvector(''icu_simple'', title) FROM heldout WHERE lang = %L', l.lang))
  WHERE char_length(word) >= 2 AND word !~ '^[0-9[:punct:]]+$'
  ORDER BY ndoc DESC, word
  OFFSET 100 LIMIT 200
) w;

SELECT format('CREATE TABLE docs_%s AS SELECT title FROM docs WHERE lang = %L', lang, lang) FROM langs ORDER BY ord \gexec
SELECT format('VACUUM ANALYZE docs_%s', lang) FROM langs ORDER BY ord \gexec

INSERT INTO results SELECT lang, 'scan only', 'docs', count(*) FROM docs GROUP BY lang;
INSERT INTO results SELECT lang, 'scan only', 'bytes', sum(octet_length(title)) FROM docs GROUP BY lang;
DROP TABLE docs;
DROP TABLE heldout;

CREATE FUNCTION index_queries(p_lang text, p_method text) RETURNS bigint
LANGUAGE plpgsql AS $$
DECLARE
  m methods;
  t text;
  line text;
  plan text;
  used bigint := 0;
BEGIN
  SELECT * INTO m FROM methods WHERE method = p_method;
  FOR t IN SELECT term FROM terms WHERE lang = p_lang LOOP
    plan := '';
    FOR line IN EXECUTE format('EXPLAIN SELECT count(*) FROM docs_%s_%s WHERE ' || m.predicate,
                            p_lang, m.ord, query_arg(p_method, t)) LOOP
      plan := plan || line;
    END LOOP;
    IF plan LIKE format('%%docs\_%s\_%s\_idx%%', p_lang, m.ord) THEN
      used := used + 1;
    END IF;
  END LOOP;
  RETURN used;
END
$$;

SELECT format('INSERT INTO results VALUES (%L, %L, ''parse_ms'', median_ms(%L, %s))',
              l.lang, m.method, format('SELECT sum(%s)::text FROM docs_%s', m.expr, l.lang), :runs)
FROM langs l CROSS JOIN methods m ORDER BY l.ord, m.ord \gexec

SELECT format('CREATE TABLE docs_%s_%s AS SELECT title FROM docs_%s', l.lang, m.ord, l.lang)
FROM langs l CROSS JOIN methods m WHERE m.index_def IS NOT NULL ORDER BY l.ord, m.ord \gexec

SELECT format('INSERT INTO results VALUES (%L, %L, ''index_ms'', elapsed_ms(%L))',
              l.lang, m.method, format('CREATE INDEX docs_%s_%s_idx ON docs_%s_%s USING %s', l.lang, m.ord, l.lang, m.ord, m.index_def))
FROM langs l CROSS JOIN methods m WHERE m.index_def IS NOT NULL ORDER BY l.ord, m.ord \gexec

INSERT INTO results
SELECT l.lang, m.method, 'index_bytes', pg_relation_size(format('docs_%s_%s_idx', l.lang, m.ord)::regclass)
FROM langs l CROSS JOIN methods m WHERE m.index_def IS NOT NULL;

SELECT format('VACUUM ANALYZE docs_%s_%s', l.lang, m.ord)
FROM langs l CROSS JOIN methods m WHERE m.index_def IS NOT NULL ORDER BY l.ord, m.ord \gexec

INSERT INTO results SELECT lang, 'scan only', 'terms', count(*) FROM terms GROUP BY lang;

SELECT format('INSERT INTO results VALUES (%L, %L, ''matches'', run_terms(%L, %L))', l.lang, m.method, l.lang, m.method)
FROM langs l CROSS JOIN methods m ORDER BY l.ord, m.ord \gexec

SELECT format('INSERT INTO results VALUES (%L, %L, ''query_ms'', median_ms(%L, %s))',
              l.lang, m.method, format('SELECT run_terms(%L, %L)::text', l.lang, m.method), :runs)
FROM langs l CROSS JOIN methods m WHERE m.predicate IS NOT NULL ORDER BY l.ord, m.ord \gexec

SELECT format('INSERT INTO results VALUES (%L, %L, ''index_queries'', index_queries(%L, %L))', l.lang, m.method, l.lang, m.method)
FROM langs l CROSS JOIN methods m WHERE m.predicate IS NOT NULL ORDER BY l.ord, m.ord \gexec
