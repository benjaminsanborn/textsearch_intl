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
| en | 500,000 | 21.0 | default parser | 0.787 | 1.00× |
| en | 500,000 | 21.0 | intl_simple | 0.685 | 0.87× |
| en | 500,000 | 21.0 | intl_search | 0.857 | 1.09× |
| en | 500,000 | 21.0 | intl_search_ngram | 0.950 | 1.21× |
| en | 500,000 | 21.0 | pg_trgm | 1.226 | 1.56× |
| en | 500,000 | 21.0 | pg_bigm | 1.010 | 1.28× |
| zh | 500,000 | 16.9 | default parser | 0.469 | 1.00× |
| zh | 500,000 | 16.9 | intl_simple | 1.606 | 3.43× |
| zh | 500,000 | 16.9 | intl_search | 1.889 | 4.03× |
| zh | 500,000 | 16.9 | intl_search_ngram | 2.096 | 4.47× |
| zh | 500,000 | 16.9 | pg_trgm | 0.804 | 1.72× |
| zh | 500,000 | 16.9 | pg_bigm | 0.512 | 1.09× |
| ja | 500,000 | 22.2 | default parser | 0.621 | 1.00× |
| ja | 500,000 | 22.2 | intl_simple | 2.006 | 3.23× |
| ja | 500,000 | 22.2 | intl_search | 2.210 | 3.56× |
| ja | 500,000 | 22.2 | intl_search_ngram | 2.612 | 4.20× |
| ja | 500,000 | 22.2 | pg_trgm | 1.079 | 1.74× |
| ja | 500,000 | 22.2 | pg_bigm | 0.707 | 1.14× |
| ko | 500,000 | 19.1 | default parser | 0.579 | 1.00× |
| ko | 500,000 | 19.1 | intl_simple | 0.717 | 1.24× |
| ko | 500,000 | 19.1 | intl_search | 0.892 | 1.54× |
| ko | 500,000 | 19.1 | intl_search_ngram | 1.271 | 2.20× |
| ko | 500,000 | 19.1 | pg_trgm | 0.949 | 1.64× |
| ko | 500,000 | 19.1 | pg_bigm | 0.618 | 1.07× |
| th | 366,332 | 45.9 | default parser | 1.155 | 1.00× |
| th | 366,332 | 45.9 | intl_simple | 1.793 | 1.55× |
| th | 366,332 | 45.9 | intl_search | 2.211 | 1.91× |
| th | 366,332 | 45.9 | intl_search_ngram | 3.407 | 2.95× |
| th | 366,332 | 45.9 | pg_trgm | 2.295 | 1.99× |
| th | 366,332 | 45.9 | pg_bigm | 1.525 | 1.32× |

## GIN index build

Single-process `CREATE INDEX` with `maintenance_work_mem = 1GB`.

| Language | Method | Build seconds | Index size MB |
|---|---|--:|--:|
| en | default parser | 1.7 | 25.5 |
| en | intl_simple | 1.6 | 24.0 |
| en | intl_search | 1.7 | 23.4 |
| en | intl_search_ngram | 1.7 | 23.5 |
| en | pg_trgm | 1.8 | 29.5 |
| en | pg_bigm | 1.8 | 20.4 |
| zh | default parser | 1.5 | 38.2 |
| zh | intl_simple | 1.8 | 14.3 |
| zh | intl_search | 1.9 | 14.3 |
| zh | intl_search_ngram | 2.3 | 18.2 |
| zh | pg_trgm | 3.1 | 72.0 |
| zh | pg_bigm | 2.9 | 41.8 |
| ja | default parser | 1.5 | 39.6 |
| ja | intl_simple | 2.1 | 18.6 |
| ja | intl_search | 2.3 | 18.6 |
| ja | intl_search_ngram | 3.1 | 20.7 |
| ja | pg_trgm | 3.0 | 63.7 |
| ja | pg_bigm | 2.6 | 33.9 |
| ko | default parser | 1.4 | 30.1 |
| ko | intl_simple | 1.5 | 28.8 |
| ko | intl_search | 1.6 | 28.8 |
| ko | intl_search_ngram | 2.5 | 24.6 |
| ko | pg_trgm | 2.4 | 48.9 |
| ko | pg_bigm | 1.9 | 24.3 |
| th | default parser | 1.4 | 37.6 |
| th | intl_simple | 1.4 | 13.7 |
| th | intl_search | 1.6 | 13.7 |
| th | intl_search_ngram | 5.0 | 45.6 |
| th | pg_trgm | 1.8 | 23.4 |
| th | pg_bigm | 1.6 | 18.8 |

## Word queries

Each language runs its query terms one at a time as `count(*)`, through each method's index. The tsvector methods use `plainto_tsquery`, and the n-gram methods use `LIKE '%term%'`. Recall and overmatch come from the labeled sample below: recall is the share of real matches the method returned, and overmatch is the share of the method's returned titles that aren't real matches.

| Language | Terms | Method | Queries using the index | Avg ms per query | Hits | Recall | Overmatch |
|---|--:|---|--:|--:|--:|--:|--:|
| en | 200 | default parser | 200 | 0.262 | 123,231 | — | — |
| en | 200 | intl_simple | 200 | 0.251 | 119,253 | — | — |
| en | 200 | intl_search | 200 | 0.252 | 119,679 | — | — |
| en | 200 | intl_search_ngram | 200 | 0.246 | 119,679 | — | — |
| en | 200 | pg_trgm | 183 | 2.947 | 659,396 | — | — |
| en | 200 | pg_bigm | 200 | 1.926 | 659,396 | — | — |
| zh | 200 | default parser | 200 | 0.034 | 5,396 | 10.3% | 0.0% |
| zh | 200 | intl_simple | 200 | 0.147 | 68,070 | 92.6% | 15.6% |
| zh | 200 | intl_search | 200 | 0.147 | 68,070 | 92.6% | 15.6% |
| zh | 200 | intl_search_ngram | 200 | 0.240 | 78,369 | 99.0% | 17.6% |
| zh | 200 | pg_trgm | 22 | 23.534 | 79,536 | 100.0% | 18.4% |
| zh | 200 | pg_bigm | 200 | 0.149 | 79,536 | 100.0% | 18.4% |
| ja | 200 | default parser | 200 | 0.039 | 7,373 | 7.0% | 0.0% |
| ja | 200 | intl_simple | 200 | 0.177 | 86,443 | 86.1% | 19.5% |
| ja | 200 | intl_search | 200 | 0.176 | 86,443 | 86.1% | 19.5% |
| ja | 200 | intl_search_ngram | 200 | 0.585 | 172,426 | 98.3% | 53.1% |
| ja | 200 | pg_trgm | 57 | 20.078 | 175,927 | 100.0% | 54.0% |
| ja | 200 | pg_bigm | 200 | 0.321 | 175,927 | 100.0% | 54.0% |
| ko | 200 | default parser | 200 | 0.089 | 35,053 | 46.7% | 0.0% |
| ko | 200 | intl_simple | 200 | 0.085 | 36,678 | 48.2% | 1.1% |
| ko | 200 | intl_search | 200 | 0.093 | 36,678 | 48.2% | 1.1% |
| ko | 200 | intl_search_ngram | 200 | 0.422 | 93,757 | 100.0% | 22.0% |
| ko | 200 | pg_trgm | 91 | 17.877 | 93,655 | 100.0% | 22.0% |
| ko | 200 | pg_bigm | 200 | 0.191 | 93,655 | 100.0% | 22.0% |
| th | 200 | default parser | 200 | 0.046 | 6,997 | 5.3% | 0.0% |
| th | 200 | intl_simple | 200 | 0.381 | 215,162 | 78.9% | 43.8% |
| th | 200 | intl_search | 200 | 0.362 | 215,162 | 78.9% | 43.8% |
| th | 200 | intl_search_ngram | 200 | 0.678 | 447,622 | 98.2% | 69.4% |
| th | 200 | pg_trgm | 133 | 17.996 | 620,044 | 100.0% | 77.2% |
| th | 200 | pg_bigm | 200 | 1.136 | 620,044 | 100.0% | 77.2% |

## Labeled sample

For each language, `bench/pairs.sql` draws term/title pairs at random from every title in the sample that contains one of the query terms as a substring, and `bench/labels.csv` marks each pair `match` or `over`. A pair is a `match` when the title uses the term as a word: on its own, with grammatical endings (Korean particles, Japanese inflection), or as part of a compound whose meaning includes it (`機場` in `國際機場`). It is `over` when the characters are there but the word isn't: inside a transliterated name (`阿拉` in `阿拉巴马州`), across a word boundary (`京都` in `東京都`), or inside an unrelated word. Every method is scored on the same pairs, so a method that returns every substring scores 100% recall and pays in overmatch.

| Language | Labeled pairs | Real matches |
|---|--:|--:|
| zh | 250 | 81.6% |
| ja | 250 | 46.0% |
| ko | 250 | 78.0% |
| th | 250 | 22.8% |
