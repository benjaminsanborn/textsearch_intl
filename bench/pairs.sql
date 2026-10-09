-- Samples term/title pairs where the title contains the term as a substring, for labeling into bench/labels.csv.
-- Run after bench/run.sh, against its database:
--   psql -X -q -d textsearch_intl_bench -v per_lang=250 -f bench/pairs.sql > bench/pairs.csv
CREATE TEMP TABLE pairs (lang text, term text, title text);

SELECT format('INSERT INTO pairs SELECT %L, t.term, d.title FROM terms t JOIN docs_%s d ON strpos(d.title, t.term) > 0 WHERE t.lang = %L',
              lang, lang, lang)
FROM langs WHERE lang <> 'en' ORDER BY ord \gexec

COPY (
  SELECT lang, term, title
  FROM (SELECT *, row_number() OVER (PARTITION BY lang ORDER BY md5(term || '|' || title)) AS rn FROM pairs) p
  WHERE rn <= :per_lang
  ORDER BY lang, term, title
) TO STDOUT WITH CSV HEADER;
