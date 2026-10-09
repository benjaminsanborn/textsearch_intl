# Benchmark results

| | |
|---|---|
| PostgreSQL | PostgreSQL 18.4 (Homebrew) on aarch64-apple-darwin24.6.0, compiled by Apple clang version 17.0.0 (clang-1700.6.4.2), 64-bit |
| ICU | 78.3 |
| CPU | Apple M4 Pro |
| Corpus | Wikipedia article titles, dump 20260901, 500000 titles per language |
| Runs | median of 5 after one warm-up run, one backend, no parallel workers, JIT off |
| LC_CTYPE | C.UTF-8 |

## Tokenizing

Time to tokenize every title once, minus the time to scan the table. "vs default" divides by the default parser's time.

| Language | Titles | Avg bytes | Method | µs per title | vs default |
|---|--:|--:|---|--:|--:|
| en | 500,000 | 21.0 | default parser | 0.773 | 1.00× |
| en | 500,000 | 21.0 | intl_simple | 0.661 | 0.85× |
| en | 500,000 | 21.0 | intl_search | 0.840 | 1.09× |
| en | 500,000 | 21.0 | intl_search_ngram | 0.900 | 1.16× |
| en | 500,000 | 21.0 | pg_trgm | 1.203 | 1.56× |
| en | 500,000 | 21.0 | pg_bigm | 0.986 | 1.28× |
| zh | 500,000 | 16.9 | default parser | 0.469 | 1.00× |
| zh | 500,000 | 16.9 | intl_simple | 1.621 | 3.45× |
| zh | 500,000 | 16.9 | intl_search | 1.858 | 3.96× |
| zh | 500,000 | 16.9 | intl_search_ngram | 2.119 | 4.52× |
| zh | 500,000 | 16.9 | pg_trgm | 0.816 | 1.74× |
| zh | 500,000 | 16.9 | pg_bigm | 0.516 | 1.10× |
| ja | 500,000 | 22.2 | default parser | 0.620 | 1.00× |
| ja | 500,000 | 22.2 | intl_simple | 1.968 | 3.17× |
| ja | 500,000 | 22.2 | intl_search | 2.179 | 3.51× |
| ja | 500,000 | 22.2 | intl_search_ngram | 2.628 | 4.24× |
| ja | 500,000 | 22.2 | pg_trgm | 1.088 | 1.75× |
| ja | 500,000 | 22.2 | pg_bigm | 0.719 | 1.16× |
| ko | 500,000 | 19.1 | default parser | 0.588 | 1.00× |
| ko | 500,000 | 19.1 | intl_simple | 0.719 | 1.22× |
| ko | 500,000 | 19.1 | intl_search | 0.890 | 1.51× |
| ko | 500,000 | 19.1 | intl_search_ngram | 1.261 | 2.14× |
| ko | 500,000 | 19.1 | pg_trgm | 0.957 | 1.63× |
| ko | 500,000 | 19.1 | pg_bigm | 0.615 | 1.05× |
| th | 366,332 | 45.9 | default parser | 1.167 | 1.00× |
| th | 366,332 | 45.9 | intl_simple | 1.859 | 1.59× |
| th | 366,332 | 45.9 | intl_search | 2.252 | 1.93× |
| th | 366,332 | 45.9 | intl_search_ngram | 2.334 | 2.00× |
| th | 366,332 | 45.9 | pg_trgm | 2.329 | 2.00× |
| th | 366,332 | 45.9 | pg_bigm | 1.559 | 1.34× |

## GIN index build

Single-process `CREATE INDEX` with `maintenance_work_mem = 1GB`.

| Language | Method | Build seconds | Index size MB |
|---|---|--:|--:|
| en | default parser | 1.8 | 25.5 |
| en | intl_simple | 1.6 | 24.0 |
| en | intl_search | 1.7 | 23.4 |
| en | intl_search_ngram | 1.7 | 23.5 |
| en | pg_trgm | 1.9 | 29.5 |
| en | pg_bigm | 1.8 | 20.4 |
| zh | default parser | 1.6 | 38.2 |
| zh | intl_simple | 1.8 | 14.3 |
| zh | intl_search | 2.0 | 14.3 |
| zh | intl_search_ngram | 2.4 | 18.2 |
| zh | pg_trgm | 2.9 | 72.0 |
| zh | pg_bigm | 2.8 | 41.8 |
| ja | default parser | 1.7 | 39.6 |
| ja | intl_simple | 2.1 | 18.6 |
| ja | intl_search | 2.3 | 18.6 |
| ja | intl_search_ngram | 3.0 | 20.7 |
| ja | pg_trgm | 3.0 | 63.7 |
| ja | pg_bigm | 2.6 | 33.9 |
| ko | default parser | 1.5 | 30.1 |
| ko | intl_simple | 1.6 | 28.8 |
| ko | intl_search | 1.6 | 28.8 |
| ko | intl_search_ngram | 3.6 | 24.6 |
| ko | pg_trgm | 2.9 | 48.9 |
| ko | pg_bigm | 2.3 | 24.3 |
| th | default parser | 2.5 | 37.6 |
| th | intl_simple | 2.6 | 13.7 |
| th | intl_search | 2.8 | 13.7 |
| th | intl_search_ngram | 2.8 | 13.7 |
| th | pg_trgm | 2.9 | 23.4 |
| th | pg_bigm | 2.7 | 18.8 |

## Word queries

Each language runs its query terms one at a time as `count(*)`, through each method's index. The tsvector methods use `plainto_tsquery`, and the n-gram methods use `LIKE '%term%'`. Recall and overmatch come from the labeled sample below: recall is the share of real matches the method returned, and overmatch is the share of the method's returned titles that aren't real matches.

| Language | Terms | Method | Queries using the index | Avg ms per query | Hits | Recall | Overmatch |
|---|--:|---|--:|--:|--:|--:|--:|
| en | 200 | default parser | 200 | 0.250 | 123,231 | — | — |
| en | 200 | intl_simple | 200 | 0.222 | 119,253 | — | — |
| en | 200 | intl_search | 200 | 0.179 | 119,679 | — | — |
| en | 200 | intl_search_ngram | 200 | 0.180 | 119,679 | — | — |
| en | 200 | pg_trgm | 183 | 2.745 | 659,396 | — | — |
| en | 200 | pg_bigm | 200 | 1.896 | 659,396 | — | — |
| zh | 200 | default parser | 200 | 0.035 | 5,396 | 10.3% | 0.0% |
| zh | 200 | intl_simple | 200 | 0.149 | 68,070 | 92.6% | 15.6% |
| zh | 200 | intl_search | 200 | 0.143 | 68,070 | 92.6% | 15.6% |
| zh | 200 | intl_search_ngram | 200 | 0.227 | 78,369 | 99.0% | 17.6% |
| zh | 200 | pg_trgm | 22 | 23.381 | 79,536 | 100.0% | 18.4% |
| zh | 200 | pg_bigm | 200 | 0.109 | 79,536 | 100.0% | 18.4% |
| ja | 200 | default parser | 200 | 0.036 | 7,373 | 7.0% | 0.0% |
| ja | 200 | intl_simple | 200 | 0.131 | 86,443 | 86.1% | 19.5% |
| ja | 200 | intl_search | 200 | 0.131 | 86,443 | 86.1% | 19.5% |
| ja | 200 | intl_search_ngram | 200 | 0.498 | 172,426 | 98.3% | 53.1% |
| ja | 200 | pg_trgm | 57 | 19.554 | 175,927 | 100.0% | 54.0% |
| ja | 200 | pg_bigm | 200 | 0.263 | 175,927 | 100.0% | 54.0% |
| ko | 200 | default parser | 200 | 0.070 | 35,053 | 46.7% | 0.0% |
| ko | 200 | intl_simple | 200 | 0.094 | 36,678 | 48.2% | 1.1% |
| ko | 200 | intl_search | 200 | 0.094 | 36,678 | 48.2% | 1.1% |
| ko | 200 | intl_search_ngram | 200 | 0.439 | 93,757 | 100.0% | 22.0% |
| ko | 200 | pg_trgm | 91 | 17.865 | 93,655 | 100.0% | 22.0% |
| ko | 200 | pg_bigm | 200 | 0.135 | 93,655 | 100.0% | 22.0% |
| th | 200 | default parser | 200 | 0.038 | 6,997 | 5.3% | 0.0% |
| th | 200 | intl_simple | 200 | 0.287 | 215,162 | 78.9% | 43.8% |
| th | 200 | intl_search | 200 | 0.285 | 215,162 | 78.9% | 43.8% |
| th | 200 | intl_search_ngram | 200 | 0.281 | 215,162 | 78.9% | 43.8% |
| th | 200 | pg_trgm | 133 | 17.111 | 620,044 | 100.0% | 77.2% |
| th | 200 | pg_bigm | 200 | 0.982 | 620,044 | 100.0% | 77.2% |

## Labeled sample

For each language, `bench/pairs.sql` draws term/title pairs at random from every title in the sample that contains one of the query terms as a substring, and `bench/labels.csv` marks each pair `match` or `over`. A pair is a `match` when the title uses the term as a word: on its own, with grammatical endings (Korean particles, Japanese inflection), or as part of a compound whose meaning includes it (`機場` in `國際機場`). It is `over` when the characters are there but the word isn't: inside a transliterated name (`阿拉` in `阿拉巴马州`), across a word boundary (`京都` in `東京都`), or inside an unrelated word. Every method is scored on the same pairs, so a method that returns every substring scores 100% recall and pays in overmatch.

| Language | Labeled pairs | Real matches |
|---|--:|--:|
| zh | 250 | 81.6% |
| ja | 250 | 46.0% |
| ko | 250 | 78.0% |
| th | 250 | 22.8% |
