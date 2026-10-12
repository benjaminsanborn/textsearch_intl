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
| en | 500,000 | 21.0 | default parser | 0.748 | 1.00× |
| en | 500,000 | 21.0 | intl_simple | 0.700 | 0.94× |
| en | 500,000 | 21.0 | intl_search | 1.000 | 1.34× |
| en | 500,000 | 21.0 | intl_search_ngram | 1.081 | 1.45× |
| en | 500,000 | 21.0 | pg_trgm | 1.218 | 1.63× |
| en | 500,000 | 21.0 | pg_bigm | 0.934 | 1.25× |
| zh | 500,000 | 16.9 | default parser | 0.455 | 1.00× |
| zh | 500,000 | 16.9 | intl_simple | 1.587 | 3.49× |
| zh | 500,000 | 16.9 | intl_search | 2.109 | 4.64× |
| zh | 500,000 | 16.9 | intl_search_ngram | 2.325 | 5.11× |
| zh | 500,000 | 16.9 | pg_trgm | 0.773 | 1.70× |
| zh | 500,000 | 16.9 | pg_bigm | 0.485 | 1.07× |
| ja | 500,000 | 22.2 | default parser | 0.602 | 1.00× |
| ja | 500,000 | 22.2 | intl_simple | 1.888 | 3.14× |
| ja | 500,000 | 22.2 | intl_search | 2.367 | 3.93× |
| ja | 500,000 | 22.2 | intl_search_ngram | 2.661 | 4.42× |
| ja | 500,000 | 22.2 | pg_trgm | 1.020 | 1.70× |
| ja | 500,000 | 22.2 | pg_bigm | 0.660 | 1.10× |
| ko | 500,000 | 19.1 | default parser | 0.555 | 1.00× |
| ko | 500,000 | 19.1 | intl_simple | 0.745 | 1.34× |
| ko | 500,000 | 19.1 | intl_search | 1.056 | 1.91× |
| ko | 500,000 | 19.1 | intl_search_ngram | 1.412 | 2.55× |
| ko | 500,000 | 19.1 | pg_trgm | 0.898 | 1.62× |
| ko | 500,000 | 19.1 | pg_bigm | 0.632 | 1.14× |
| th | 366,332 | 45.9 | default parser | 1.094 | 1.00× |
| th | 366,332 | 45.9 | intl_simple | 1.831 | 1.67× |
| th | 366,332 | 45.9 | intl_search | 2.498 | 2.28× |
| th | 366,332 | 45.9 | intl_search_ngram | 3.434 | 3.14× |
| th | 366,332 | 45.9 | pg_trgm | 2.235 | 2.04× |
| th | 366,332 | 45.9 | pg_bigm | 1.576 | 1.44× |

## GIN index build

Single-process `CREATE INDEX` with `maintenance_work_mem = 1GB`.

| Language | Method | Build seconds | Index size MB |
|---|---|--:|--:|
| en | default parser | 1.4 | 25.5 |
| en | intl_simple | 1.3 | 23.7 |
| en | intl_search | 1.5 | 23.3 |
| en | intl_search_ngram | 1.5 | 23.4 |
| en | pg_trgm | 1.7 | 29.5 |
| en | pg_bigm | 1.7 | 20.4 |
| zh | default parser | 1.3 | 38.2 |
| zh | intl_simple | 1.6 | 14.0 |
| zh | intl_search | 1.8 | 13.3 |
| zh | intl_search_ngram | 2.2 | 17.1 |
| zh | pg_trgm | 2.7 | 72.0 |
| zh | pg_bigm | 2.5 | 41.8 |
| ja | default parser | 1.4 | 39.6 |
| ja | intl_simple | 1.7 | 18.4 |
| ja | intl_search | 2.0 | 18.2 |
| ja | intl_search_ngram | 3.0 | 20.7 |
| ja | pg_trgm | 2.9 | 63.7 |
| ja | pg_bigm | 2.5 | 33.9 |
| ko | default parser | 1.4 | 30.1 |
| ko | intl_simple | 1.6 | 28.6 |
| ko | intl_search | 1.7 | 28.7 |
| ko | intl_search_ngram | 2.6 | 24.5 |
| ko | pg_trgm | 2.3 | 48.9 |
| ko | pg_bigm | 2.0 | 24.3 |
| th | default parser | 1.5 | 37.6 |
| th | intl_simple | 1.5 | 13.7 |
| th | intl_search | 2.2 | 13.6 |
| th | intl_search_ngram | 5.8 | 45.5 |
| th | pg_trgm | 1.6 | 23.4 |
| th | pg_bigm | 1.6 | 18.8 |

## Word queries

Each language runs its query terms one at a time as `count(*)`, through each method's index. The tsvector methods use `plainto_tsquery`, and the n-gram methods use `LIKE '%term%'`. Recall and overmatch come from the labeled sample below: recall is the share of real matches the method returned, and overmatch is the share of the method's returned titles that aren't real matches.

| Language | Terms | Method | Queries using the index | Avg ms per query | Hits | Recall | Overmatch |
|---|--:|---|--:|--:|--:|--:|--:|
| en | 200 | default parser | 200 | 0.234 | 123,406 | — | — |
| en | 200 | intl_simple | 200 | 0.275 | 119,996 | — | — |
| en | 200 | intl_search | 200 | 0.266 | 121,265 | — | — |
| en | 200 | intl_search_ngram | 200 | 0.272 | 121,265 | — | — |
| en | 200 | pg_trgm | 182 | 3.060 | 708,250 | — | — |
| en | 200 | pg_bigm | 200 | 2.063 | 708,250 | — | — |
| zh | 200 | default parser | 200 | 0.037 | 5,396 | 10.3% | 0.0% |
| zh | 200 | intl_simple | 200 | 0.164 | 68,070 | 92.6% | 15.6% |
| zh | 200 | intl_search | 200 | 0.185 | 97,880 | 92.6% | 15.6% |
| zh | 200 | intl_search_ngram | 200 | 0.336 | 111,835 | 99.0% | 17.6% |
| zh | 200 | pg_trgm | 22 | 23.104 | 79,536 | 100.0% | 18.4% |
| zh | 200 | pg_bigm | 200 | 0.111 | 79,536 | 100.0% | 18.4% |
| ja | 200 | default parser | 200 | 0.036 | 7,373 | 7.0% | 0.0% |
| ja | 200 | intl_simple | 200 | 0.132 | 86,443 | 86.1% | 19.5% |
| ja | 200 | intl_search | 200 | 0.140 | 88,779 | 86.1% | 19.5% |
| ja | 200 | intl_search_ngram | 200 | 0.530 | 184,736 | 100.0% | 53.6% |
| ja | 200 | pg_trgm | 57 | 19.852 | 175,927 | 100.0% | 54.0% |
| ja | 200 | pg_bigm | 200 | 0.285 | 175,927 | 100.0% | 54.0% |
| ko | 200 | default parser | 200 | 0.070 | 35,053 | 46.7% | 0.0% |
| ko | 200 | intl_simple | 200 | 0.099 | 36,678 | 48.2% | 1.1% |
| ko | 200 | intl_search | 200 | 0.082 | 36,678 | 48.2% | 1.1% |
| ko | 200 | intl_search_ngram | 200 | 0.398 | 93,757 | 100.0% | 22.0% |
| ko | 200 | pg_trgm | 91 | 17.704 | 93,655 | 100.0% | 22.0% |
| ko | 200 | pg_bigm | 200 | 0.139 | 93,655 | 100.0% | 22.0% |
| th | 200 | default parser | 200 | 0.037 | 6,997 | 5.3% | 0.0% |
| th | 200 | intl_simple | 200 | 0.294 | 215,162 | 78.9% | 43.8% |
| th | 200 | intl_search | 200 | 0.292 | 215,162 | 78.9% | 43.8% |
| th | 200 | intl_search_ngram | 200 | 0.586 | 447,620 | 98.2% | 69.4% |
| th | 200 | pg_trgm | 133 | 17.208 | 620,044 | 100.0% | 77.2% |
| th | 200 | pg_bigm | 200 | 1.089 | 620,044 | 100.0% | 77.2% |

## Labeled sample

For each language, `bench/pairs.sql` draws term/title pairs at random from every title in the sample that contains one of the query terms as a substring, and `bench/labels.csv` marks each pair `match` or `over`. A pair is a `match` when the title uses the term as a word: on its own, with grammatical endings (Korean particles, Japanese inflection), or as part of a compound whose meaning includes it (`機場` in `國際機場`). It is `over` when the characters are there but the word isn't: inside a transliterated name (`阿拉` in `阿拉巴马州`), across a word boundary (`京都` in `東京都`), or inside an unrelated word. Every method is scored on the same pairs, so a method that returns every substring scores 100% recall and pays in overmatch.

| Language | Labeled pairs | Real matches |
|---|--:|--:|
| zh | 250 | 81.6% |
| ja | 250 | 46.0% |
| ko | 250 | 78.0% |
| th | 250 | 22.8% |
