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
| en | 500,000 | 21.0 | default parser | 0.695 | 1.00× |
| en | 500,000 | 21.0 | intl_simple | 0.608 | 0.87× |
| en | 500,000 | 21.0 | intl_search | 0.796 | 1.15× |
| en | 500,000 | 21.0 | intl_search_ngram | 0.872 | 1.26× |
| en | 500,000 | 21.0 | pg_trgm | 1.110 | 1.60× |
| en | 500,000 | 21.0 | pg_bigm | 0.877 | 1.26× |
| zh | 500,000 | 16.9 | default parser | 0.421 | 1.00× |
| zh | 500,000 | 16.9 | intl_simple | 1.413 | 3.36× |
| zh | 500,000 | 16.9 | intl_search | 1.729 | 4.11× |
| zh | 500,000 | 16.9 | intl_search_ngram | 1.927 | 4.58× |
| zh | 500,000 | 16.9 | pg_trgm | 0.750 | 1.78× |
| zh | 500,000 | 16.9 | pg_bigm | 0.475 | 1.13× |
| ja | 500,000 | 22.2 | default parser | 0.594 | 1.00× |
| ja | 500,000 | 22.2 | intl_simple | 1.853 | 3.12× |
| ja | 500,000 | 22.2 | intl_search | 2.092 | 3.52× |
| ja | 500,000 | 22.2 | intl_search_ngram | 2.524 | 4.25× |
| ja | 500,000 | 22.2 | pg_trgm | 1.088 | 1.83× |
| ja | 500,000 | 22.2 | pg_bigm | 0.680 | 1.14× |
| ko | 500,000 | 19.1 | default parser | 0.567 | 1.00× |
| ko | 500,000 | 19.1 | intl_simple | 0.692 | 1.22× |
| ko | 500,000 | 19.1 | intl_search | 0.900 | 1.59× |
| ko | 500,000 | 19.1 | intl_search_ngram | 1.284 | 2.26× |
| ko | 500,000 | 19.1 | pg_trgm | 0.929 | 1.64× |
| ko | 500,000 | 19.1 | pg_bigm | 0.604 | 1.06× |
| th | 366,332 | 45.9 | default parser | 1.143 | 1.00× |
| th | 366,332 | 45.9 | intl_simple | 1.797 | 1.57× |
| th | 366,332 | 45.9 | intl_search | 2.262 | 1.98× |
| th | 366,332 | 45.9 | intl_search_ngram | 3.436 | 3.01× |
| th | 366,332 | 45.9 | pg_trgm | 2.291 | 2.00× |
| th | 366,332 | 45.9 | pg_bigm | 1.512 | 1.32× |

## GIN index build

Single-process `CREATE INDEX` with `maintenance_work_mem = 1GB`.

| Language | Method | Build seconds | Index size MB |
|---|---|--:|--:|
| en | default parser | 1.7 | 25.5 |
| en | intl_simple | 1.6 | 24.0 |
| en | intl_search | 1.6 | 23.5 |
| en | intl_search_ngram | 1.6 | 23.6 |
| en | pg_trgm | 1.8 | 29.5 |
| en | pg_bigm | 1.8 | 20.4 |
| zh | default parser | 1.6 | 38.2 |
| zh | intl_simple | 1.7 | 14.3 |
| zh | intl_search | 1.9 | 14.3 |
| zh | intl_search_ngram | 2.2 | 18.2 |
| zh | pg_trgm | 3.0 | 72.0 |
| zh | pg_bigm | 3.2 | 41.8 |
| ja | default parser | 1.7 | 39.6 |
| ja | intl_simple | 2.0 | 18.6 |
| ja | intl_search | 2.2 | 18.6 |
| ja | intl_search_ngram | 2.9 | 20.7 |
| ja | pg_trgm | 3.1 | 63.7 |
| ja | pg_bigm | 2.4 | 33.9 |
| ko | default parser | 1.6 | 30.1 |
| ko | intl_simple | 1.7 | 28.8 |
| ko | intl_search | 1.6 | 28.8 |
| ko | intl_search_ngram | 2.3 | 24.6 |
| ko | pg_trgm | 1.9 | 48.9 |
| ko | pg_bigm | 1.8 | 24.3 |
| th | default parser | 1.3 | 37.6 |
| th | intl_simple | 1.4 | 13.7 |
| th | intl_search | 1.5 | 13.7 |
| th | intl_search_ngram | 4.1 | 45.5 |
| th | pg_trgm | 1.6 | 23.4 |
| th | pg_bigm | 1.6 | 18.8 |

## Word queries

Each language runs its query terms one at a time as `count(*)`, through each method's index. The tsvector methods use `plainto_tsquery`, and the n-gram methods use `LIKE '%term%'`. Recall and overmatch come from the labeled sample below: recall is the share of real matches the method returned, and overmatch is the share of the method's returned titles that aren't real matches.

| Language | Terms | Method | Queries using the index | Avg ms per query | Hits | Recall | Overmatch |
|---|--:|---|--:|--:|--:|--:|--:|
| en | 200 | default parser | 200 | 0.168 | 123,231 | — | — |
| en | 200 | intl_simple | 200 | 0.165 | 119,253 | — | — |
| en | 200 | intl_search | 200 | 0.196 | 119,699 | — | — |
| en | 200 | intl_search_ngram | 200 | 0.170 | 119,699 | — | — |
| en | 200 | pg_trgm | 183 | 2.645 | 659,396 | — | — |
| en | 200 | pg_bigm | 200 | 1.732 | 659,396 | — | — |
| zh | 200 | default parser | 200 | 0.028 | 5,396 | 10.3% | 0.0% |
| zh | 200 | intl_simple | 200 | 0.102 | 68,070 | 92.6% | 15.6% |
| zh | 200 | intl_search | 200 | 0.102 | 68,070 | 92.6% | 15.6% |
| zh | 200 | intl_search_ngram | 200 | 0.181 | 78,369 | 99.0% | 17.6% |
| zh | 200 | pg_trgm | 22 | 23.416 | 79,536 | 100.0% | 18.4% |
| zh | 200 | pg_bigm | 200 | 0.142 | 79,536 | 100.0% | 18.4% |
| ja | 200 | default parser | 200 | 0.037 | 7,373 | 7.0% | 0.0% |
| ja | 200 | intl_simple | 200 | 0.164 | 86,443 | 86.1% | 19.5% |
| ja | 200 | intl_search | 200 | 0.166 | 86,443 | 86.1% | 19.5% |
| ja | 200 | intl_search_ngram | 200 | 0.537 | 172,426 | 98.3% | 53.1% |
| ja | 200 | pg_trgm | 57 | 20.089 | 175,927 | 100.0% | 54.0% |
| ja | 200 | pg_bigm | 200 | 0.325 | 175,927 | 100.0% | 54.0% |
| ko | 200 | default parser | 200 | 0.077 | 35,053 | 46.7% | 0.0% |
| ko | 200 | intl_simple | 200 | 0.090 | 36,678 | 48.2% | 1.1% |
| ko | 200 | intl_search | 200 | 0.086 | 36,678 | 48.2% | 1.1% |
| ko | 200 | intl_search_ngram | 200 | 0.406 | 93,757 | 100.0% | 22.0% |
| ko | 200 | pg_trgm | 91 | 18.331 | 93,655 | 100.0% | 22.0% |
| ko | 200 | pg_bigm | 200 | 0.180 | 93,655 | 100.0% | 22.0% |
| th | 200 | default parser | 200 | 0.041 | 6,997 | 5.3% | 0.0% |
| th | 200 | intl_simple | 200 | 0.369 | 215,162 | 78.9% | 43.8% |
| th | 200 | intl_search | 200 | 0.375 | 215,162 | 78.9% | 43.8% |
| th | 200 | intl_search_ngram | 200 | 0.666 | 447,620 | 98.2% | 69.4% |
| th | 200 | pg_trgm | 133 | 17.734 | 620,044 | 100.0% | 77.2% |
| th | 200 | pg_bigm | 200 | 1.265 | 620,044 | 100.0% | 77.2% |

## Labeled sample

For each language, `bench/pairs.sql` draws term/title pairs at random from every title in the sample that contains one of the query terms as a substring, and `bench/labels.csv` marks each pair `match` or `over`. A pair is a `match` when the title uses the term as a word: on its own, with grammatical endings (Korean particles, Japanese inflection), or as part of a compound whose meaning includes it (`機場` in `國際機場`). It is `over` when the characters are there but the word isn't: inside a transliterated name (`阿拉` in `阿拉巴马州`), across a word boundary (`京都` in `東京都`), or inside an unrelated word. Every method is scored on the same pairs, so a method that returns every substring scores 100% recall and pays in overmatch.

| Language | Labeled pairs | Real matches |
|---|--:|--:|
| zh | 250 | 81.6% |
| ja | 250 | 46.0% |
| ko | 250 | 78.0% |
| th | 250 | 22.8% |
