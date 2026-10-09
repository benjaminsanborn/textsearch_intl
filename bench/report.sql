CREATE TEMP VIEW r AS
SELECT l.ord AS lord, m.ord AS mord, res.lang, res.method,
       max(value) FILTER (WHERE metric = 'parse_ms') AS parse_ms,
       max(value) FILTER (WHERE metric = 'index_ms') AS index_ms,
       max(value) FILTER (WHERE metric = 'index_bytes') AS index_bytes,
       max(value) FILTER (WHERE metric = 'matches') AS matches,
       max(value) FILTER (WHERE metric = 'query_ms') AS query_ms,
       max(value) FILTER (WHERE metric = 'index_queries') AS index_queries
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
       (SELECT matches FROM r WHERE r.lang = res.lang AND r.method = 'scan only') AS substring_matches
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
    || 'Each language runs its query terms one at a time as `count(*)`, through each method''s index. The tsvector methods use `plainto_tsquery`, and the n-gram methods use `LIKE ''%term%''`. "Substring matches found" is the method''s total hits divided by the number of titles that contain the term as a substring. The n-gram methods match substrings by definition, so they are always at 100%. A tsvector method only counts a hit when the term lines up with its word boundaries, so its share tells you how often segmentation finds the term where it appears.' || E'\n\n'
    || '| Language | Terms | Method | Queries using the index | Avg ms per query | Hits | Substring matches found |' || E'\n'
    || '|---|--:|---|--:|--:|--:|--:|' || E'\n'
    || string_agg(format('| %s | %s | %s | %s | %s | %s | %s |', r.lang, b.terms, r.method, r.index_queries,
         round(r.query_ms / b.terms, 3), to_char(r.matches, 'FM999,999,999'),
         round(100 * r.matches / nullif(b.substring_matches, 0), 1) || '%'),
       E'\n' ORDER BY r.lord, r.mord)
FROM r JOIN base b USING (lang) WHERE r.query_ms IS NOT NULL;
