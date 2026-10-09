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
| en | 500,000 | 21.0 | default parser | 0.730 | 1.00× |
| en | 500,000 | 21.0 | icu_parser | 0.631 | 0.86× |
| en | 500,000 | 21.0 | pg_trgm | 1.107 | 1.52× |
| en | 500,000 | 21.0 | pg_bigm | 0.908 | 1.24× |
| zh | 500,000 | 16.9 | default parser | 0.434 | 1.00× |
| zh | 500,000 | 16.9 | icu_parser | 1.438 | 3.31× |
| zh | 500,000 | 16.9 | pg_trgm | 0.762 | 1.76× |
| zh | 500,000 | 16.9 | pg_bigm | 0.484 | 1.11× |
| ja | 500,000 | 22.2 | default parser | 0.569 | 1.00× |
| ja | 500,000 | 22.2 | icu_parser | 1.718 | 3.02× |
| ja | 500,000 | 22.2 | pg_trgm | 1.011 | 1.78× |
| ja | 500,000 | 22.2 | pg_bigm | 0.659 | 1.16× |
| ko | 500,000 | 19.1 | default parser | 0.547 | 1.00× |
| ko | 500,000 | 19.1 | icu_parser | 0.667 | 1.22× |
| ko | 500,000 | 19.1 | pg_trgm | 0.893 | 1.63× |
| ko | 500,000 | 19.1 | pg_bigm | 0.580 | 1.06× |
| th | 366,332 | 45.9 | default parser | 1.083 | 1.00× |
| th | 366,332 | 45.9 | icu_parser | 1.681 | 1.55× |
| th | 366,332 | 45.9 | pg_trgm | 2.184 | 2.02× |
| th | 366,332 | 45.9 | pg_bigm | 1.432 | 1.32× |

## GIN index build

Single-process `CREATE INDEX` with `maintenance_work_mem = 1GB`.

| Language | Method | Build seconds | Index size MB |
|---|---|--:|--:|
| en | default parser | 1.4 | 25.5 |
| en | icu_parser | 1.3 | 24.0 |
| en | pg_trgm | 1.7 | 29.5 |
| en | pg_bigm | 1.7 | 20.4 |
| zh | default parser | 1.2 | 38.2 |
| zh | icu_parser | 1.5 | 14.3 |
| zh | pg_trgm | 2.2 | 72.0 |
| zh | pg_bigm | 2.1 | 41.8 |
| ja | default parser | 1.3 | 39.6 |
| ja | icu_parser | 1.7 | 18.6 |
| ja | pg_trgm | 2.3 | 63.7 |
| ja | pg_bigm | 2.0 | 33.9 |
| ko | default parser | 1.2 | 30.1 |
| ko | icu_parser | 1.3 | 28.8 |
| ko | pg_trgm | 1.8 | 48.9 |
| ko | pg_bigm | 1.5 | 24.3 |
| th | default parser | 1.2 | 37.6 |
| th | icu_parser | 1.3 | 13.7 |
| th | pg_trgm | 1.6 | 23.4 |
| th | pg_bigm | 1.6 | 18.8 |

## Word queries

Each language runs its query terms one at a time as `count(*)`, through each method's index. The tsvector methods use `plainto_tsquery`, and the n-gram methods use `LIKE '%term%'`. Recall and overmatch come from the labeled sample below: recall is the share of real matches the method returned, and overmatch is the share of the method's returned titles that aren't real matches.

| Language | Terms | Method | Queries using the index | Avg ms per query | Hits | Recall | Overmatch |
|---|--:|---|--:|--:|--:|--:|--:|
| en | 200 | default parser | 200 | 0.167 | 123,231 | — | — |
| en | 200 | icu_parser | 200 | 0.163 | 119,253 | — | — |
| en | 200 | pg_trgm | 183 | 2.564 | 659,396 | — | — |
| en | 200 | pg_bigm | 200 | 1.698 | 659,396 | — | — |
| zh | 200 | default parser | 200 | 0.027 | 5,396 | 10.3% | 0.0% |
| zh | 200 | icu_parser | 200 | 0.106 | 68,070 | 92.6% | 15.6% |
| zh | 200 | pg_trgm | 22 | 21.677 | 79,536 | 100.0% | 18.4% |
| zh | 200 | pg_bigm | 200 | 0.105 | 79,536 | 100.0% | 18.4% |
| ja | 200 | default parser | 200 | 0.035 | 7,373 | 7.0% | 0.0% |
| ja | 200 | icu_parser | 200 | 0.128 | 86,443 | 86.1% | 19.5% |
| ja | 200 | pg_trgm | 57 | 18.650 | 175,927 | 100.0% | 54.0% |
| ja | 200 | pg_bigm | 200 | 0.245 | 175,927 | 100.0% | 54.0% |
| ko | 200 | default parser | 200 | 0.064 | 35,053 | 46.7% | 0.0% |
| ko | 200 | icu_parser | 200 | 0.067 | 36,678 | 48.2% | 1.1% |
| ko | 200 | pg_trgm | 91 | 16.965 | 93,655 | 100.0% | 22.0% |
| ko | 200 | pg_bigm | 200 | 0.135 | 93,655 | 100.0% | 22.0% |
| th | 200 | default parser | 200 | 0.037 | 6,997 | 5.3% | 0.0% |
| th | 200 | icu_parser | 200 | 0.279 | 215,162 | 78.9% | 43.8% |
| th | 200 | pg_trgm | 133 | 16.912 | 620,044 | 100.0% | 77.2% |
| th | 200 | pg_bigm | 200 | 0.967 | 620,044 | 100.0% | 77.2% |

## Labeled sample

For each language, `bench/pairs.sql` draws term/title pairs at random from every title in the sample that contains one of the query terms as a substring, and `bench/labels.csv` marks each pair `match` or `over`. A pair is a `match` when the title uses the term as a word: on its own, with grammatical endings (Korean particles, Japanese inflection), or as part of a compound whose meaning includes it (`機場` in `國際機場`). It is `over` when the characters are there but the word isn't: inside a transliterated name (`阿拉` in `阿拉巴马州`), across a word boundary (`京都` in `東京都`), or inside an unrelated word. Every method is scored on the same pairs, so a method that returns every substring scores 100% recall and pays in overmatch.

| Language | Labeled pairs | Real matches |
|---|--:|--:|
| zh | 250 | 81.6% |
| ja | 250 | 46.0% |
| ko | 250 | 78.0% |
| th | 250 | 22.8% |
