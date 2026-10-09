CREATE TEMP VIEW r AS
SELECT l.ord AS lord, m.ord AS mord, res.lang, res.method,
       max(value) FILTER (WHERE metric = 'parse_ms') AS parse_ms,
       max(value) FILTER (WHERE metric = 'index_ms') AS index_ms,
       max(value) FILTER (WHERE metric = 'index_bytes') AS index_bytes,
       max(value) FILTER (WHERE metric = 'matches') AS matches,
       max(value) FILTER (WHERE metric = 'query_ms') AS query_ms,
       max(value) FILTER (WHERE metric = 'index_queries') AS index_queries,
       max(value) FILTER (WHERE metric = 'recall') AS recall,
       max(value) FILTER (WHERE metric = 'overmatch') AS overmatch
FROM results res
JOIN langs l USING (lang)
JOIN methods m USING (method)
GROUP BY 1, 2, 3, 4;

CREATE TEMP VIEW base AS
SELECT lang,
       max(value) FILTER (WHERE metric = 'docs') AS docs,
       max(value) FILTER (WHERE metric = 'bytes') AS bytes,
       max(value) FILTER (WHERE metric = 'terms') AS terms,
       (SELECT parse_ms FROM r WHERE r.lang = res.lang AND r.method = 'scan only') AS scan_ms,
       (SELECT parse_ms FROM r WHERE r.lang = res.lang AND r.method = 'default parser') AS default_ms,
       max(value) FILTER (WHERE metric = 'labeled') AS labeled,
       max(value) FILTER (WHERE metric = 'labeled_match') AS labeled_match
FROM results res WHERE method = 'scan only' GROUP BY lang;

SELECT '## Tokenizing' || E'\n\n'
    || 'Time to tokenize every title once, minus the time to scan the table. "vs default" divides by the default parser''s time.' || E'\n\n'
    || '| Language | Titles | Avg bytes | Method | µs per title | vs default |' || E'\n'
    || '|---|--:|--:|---|--:|--:|' || E'\n'
    || string_agg(format('| %s | %s | %s | %s | %s | %s |',
         r.lang, to_char(b.docs, 'FM999,999,999'), round(b.bytes / b.docs, 1), r.method,
         round((r.parse_ms - b.scan_ms) * 1000 / b.docs, 3),
         round((r.parse_ms - b.scan_ms) / nullif(b.default_ms - b.scan_ms, 0), 2) || '×'),
       E'\n' ORDER BY r.lord, r.mord)
FROM r JOIN base b USING (lang) WHERE r.method <> 'scan only';

SELECT E'\n## GIN index build\n\n'
    || 'Single-process `CREATE INDEX` with `maintenance_work_mem = 1GB`.' || E'\n\n'
    || '| Language | Method | Build seconds | Index size MB |' || E'\n'
    || '|---|---|--:|--:|' || E'\n'
    || string_agg(format('| %s | %s | %s | %s |', r.lang, r.method,
         round(r.index_ms / 1000, 1), round(r.index_bytes / 1048576, 1)),
       E'\n' ORDER BY r.lord, r.mord)
FROM r WHERE r.index_ms IS NOT NULL;

SELECT E'\n## Word queries\n\n'
    || 'Each language runs its query terms one at a time as `count(*)`, through each method''s index. The tsvector methods use `plainto_tsquery`, and the n-gram methods use `LIKE ''%term%''`. Recall and overmatch come from the labeled sample below: recall is the share of real matches the method returned, and overmatch is the share of the method''s returned titles that aren''t real matches.' || E'\n\n'
    || '| Language | Terms | Method | Queries using the index | Avg ms per query | Hits | Recall | Overmatch |' || E'\n'
    || '|---|--:|---|--:|--:|--:|--:|--:|' || E'\n'
    || string_agg(format('| %s | %s | %s | %s | %s | %s | %s | %s |', r.lang, b.terms, r.method, r.index_queries,
         round(r.query_ms / b.terms, 3), to_char(r.matches, 'FM999,999,999'),
         coalesce(round(r.recall, 1) || '%', '—'), coalesce(round(r.overmatch, 1) || '%', '—')),
       E'\n' ORDER BY r.lord, r.mord)
FROM r JOIN base b USING (lang) WHERE r.query_ms IS NOT NULL;

SELECT E'\n## Labeled sample\n\n'
    || 'For each language, `bench/pairs.sql` draws term/title pairs at random from every title in the sample that contains one of the query terms as a substring, and `bench/labels.csv` marks each pair `match` or `over`. A pair is a `match` when the title uses the term as a word: on its own, with grammatical endings (Korean particles, Japanese inflection), or as part of a compound whose meaning includes it (`機場` in `國際機場`). It is `over` when the characters are there but the word isn''t: inside a transliterated name (`阿拉` in `阿拉巴马州`), across a word boundary (`京都` in `東京都`), or inside an unrelated word. Every method is scored on the same pairs, so a method that returns every substring scores 100% recall and pays in overmatch.' || E'\n\n'
    || '| Language | Labeled pairs | Real matches |' || E'\n'
    || '|---|--:|--:|' || E'\n'
    || string_agg(format('| %s | %s | %s%% |', b.lang, b.labeled, round(100 * b.labeled_match / b.labeled, 1)),
       E'\n' ORDER BY l.ord)
FROM base b JOIN langs l USING (lang) WHERE b.labeled > 0;
