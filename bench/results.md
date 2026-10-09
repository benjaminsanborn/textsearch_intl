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
| en | 500,000 | 21.0 | default parser | 0.718 | 1.00× |
| en | 500,000 | 21.0 | icu_parser | 0.614 | 0.85× |
| en | 500,000 | 21.0 | pg_trgm | 1.093 | 1.52× |
| en | 500,000 | 21.0 | pg_bigm | 0.900 | 1.25× |
| zh | 500,000 | 16.9 | default parser | 0.430 | 1.00× |
| zh | 500,000 | 16.9 | icu_parser | 1.419 | 3.30× |
| zh | 500,000 | 16.9 | pg_trgm | 0.759 | 1.76× |
| zh | 500,000 | 16.9 | pg_bigm | 0.478 | 1.11× |
| ja | 500,000 | 22.2 | default parser | 0.567 | 1.00× |
| ja | 500,000 | 22.2 | icu_parser | 1.696 | 2.99× |
| ja | 500,000 | 22.2 | pg_trgm | 1.004 | 1.77× |
| ja | 500,000 | 22.2 | pg_bigm | 0.652 | 1.15× |
| ko | 500,000 | 19.1 | default parser | 0.544 | 1.00× |
| ko | 500,000 | 19.1 | icu_parser | 0.655 | 1.20× |
| ko | 500,000 | 19.1 | pg_trgm | 0.887 | 1.63× |
| ko | 500,000 | 19.1 | pg_bigm | 0.576 | 1.06× |
| th | 366,332 | 45.9 | default parser | 1.082 | 1.00× |
| th | 366,332 | 45.9 | icu_parser | 1.668 | 1.54× |
| th | 366,332 | 45.9 | pg_trgm | 2.172 | 2.01× |
| th | 366,332 | 45.9 | pg_bigm | 1.423 | 1.31× |

## GIN index build

Single-process `CREATE INDEX` with `maintenance_work_mem = 1GB`.

| Language | Method | Build seconds | Index size MB |
|---|---|--:|--:|
| en | default parser | 1.4 | 25.5 |
| en | icu_parser | 1.2 | 24.0 |
| en | pg_trgm | 1.7 | 29.5 |
| en | pg_bigm | 1.7 | 20.4 |
| zh | default parser | 1.2 | 38.2 |
| zh | icu_parser | 1.5 | 14.3 |
| zh | pg_trgm | 2.2 | 72.0 |
| zh | pg_bigm | 2.0 | 41.8 |
| ja | default parser | 1.3 | 39.6 |
| ja | icu_parser | 1.7 | 18.6 |
| ja | pg_trgm | 2.2 | 63.7 |
| ja | pg_bigm | 1.9 | 33.9 |
| ko | default parser | 1.2 | 30.1 |
| ko | icu_parser | 1.2 | 28.8 |
| ko | pg_trgm | 1.8 | 48.9 |
| ko | pg_bigm | 1.5 | 24.3 |
| th | default parser | 1.2 | 37.6 |
| th | icu_parser | 1.3 | 13.7 |
| th | pg_trgm | 1.6 | 23.4 |
| th | pg_bigm | 1.6 | 18.8 |

## Word queries

Each language runs its query terms one at a time as `count(*)`, through each method's index. The tsvector methods use `plainto_tsquery`, and the n-gram methods use `LIKE '%term%'`. "Substring matches found" is the method's total hits divided by the number of titles that contain the term as a substring. The n-gram methods match substrings by definition, so they are always at 100%. A tsvector method only counts a hit when the term lines up with its word boundaries, so its share tells you how often segmentation finds the term where it appears.

| Language | Terms | Method | Queries using the index | Avg ms per query | Hits | Substring matches found |
|---|--:|---|--:|--:|--:|--:|
| en | 200 | default parser | 200 | 0.170 | 125,146 | 18.9% |
| en | 200 | icu_parser | 200 | 0.165 | 121,191 | 18.3% |
| en | 200 | pg_trgm | 183 | 2.694 | 661,708 | 100.0% |
| en | 200 | pg_bigm | 200 | 1.687 | 661,708 | 100.0% |
| zh | 200 | default parser | 200 | 0.028 | 6,961 | 7.9% |
| zh | 200 | icu_parser | 200 | 0.102 | 69,579 | 78.7% |
| zh | 200 | pg_trgm | 24 | 23.581 | 88,360 | 100.0% |
| zh | 200 | pg_bigm | 200 | 0.127 | 88,360 | 100.0% |
| ja | 200 | default parser | 200 | 0.040 | 9,652 | 5.5% |
| ja | 200 | icu_parser | 200 | 0.137 | 88,157 | 49.9% |
| ja | 200 | pg_trgm | 59 | 18.560 | 176,667 | 100.0% |
| ja | 200 | pg_bigm | 200 | 0.236 | 176,667 | 100.0% |
| ko | 200 | default parser | 200 | 0.068 | 38,150 | 39.0% |
| ko | 200 | icu_parser | 200 | 0.070 | 40,252 | 41.2% |
| ko | 200 | pg_trgm | 95 | 15.723 | 97,710 | 100.0% |
| ko | 200 | pg_bigm | 200 | 0.136 | 97,710 | 100.0% |
| th | 200 | default parser | 200 | 0.037 | 7,023 | 1.1% |
| th | 200 | icu_parser | 200 | 0.272 | 219,691 | 35.2% |
| th | 200 | pg_trgm | 133 | 16.568 | 624,488 | 100.0% |
| th | 200 | pg_bigm | 200 | 0.939 | 624,488 | 100.0% |
