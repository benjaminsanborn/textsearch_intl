# icu_parser

A PostgreSQL text search parser that splits text into words with ICU word boundaries.

PostgreSQL's built-in `default` parser only splits on spaces and punctuation, so text in scripts written without spaces between words comes out as a single token. Chinese, Japanese, Thai, Khmer, Lao and Burmese all have this problem:

```sql
SELECT token FROM ts_debug('simple', '東京駅前ラーメン店');
--  東京駅前ラーメン店
```

`icu_parser` uses ICU's word break iterator, which uses dictionaries to segment those scripts:

```sql
CREATE EXTENSION icu_parser;

SELECT alias, token FROM ts_debug('icu_simple', '東京駅前ラーメン店 ร้านกาแฟ Café');
--  ideo  | 東京
--  ideo  | 駅前
--  ideo  | ラーメン
--  ideo  | 店
--  blank |
--  word  | ร้าน
--  word  | กาแฟ
--  blank |
--  word  | Café

SELECT to_tsvector('icu_simple', '東京駅前ラーメン店') @@ plainto_tsquery('icu_simple', 'ラーメン');
--  t
```

## Requirements

- PostgreSQL 14 or later, in a `UTF8` database
- ICU development headers (`libicu-dev`, `libicu-devel` or Homebrew `icu4c`) and `pkg-config`

## Install

```sh
make
make install
make installcheck
```

Point `PG_CONFIG` at a specific server's `pg_config` if more than one is installed. If `pkg-config` can't find ICU, set `PKG_CONFIG_PATH`, or pass `ICU_CFLAGS` and `ICU_LIBS` yourself. With Homebrew on macOS:

```sh
PKG_CONFIG_PATH="$(brew --prefix icu4c)/lib/pkgconfig" make PG_SYSROOT="$(xcrun --show-sdk-path)"
```

## Usage

The extension creates the parser `icu` and the configuration `icu_simple`, which maps every word token to the `simple` dictionary (lowercase, no stemming, no stop words).

```sql
CREATE INDEX places_name_icu_idx ON places USING gin (to_tsvector('icu_simple', name));

SELECT * FROM places
WHERE to_tsvector('icu_simple', name) @@ websearch_to_tsquery('icu_simple', 'ラーメン');
```

Build your own configuration on the parser to add stemming, stop words or accent folding:

```sql
CREATE EXTENSION unaccent;
CREATE TEXT SEARCH CONFIGURATION icu_unaccent (PARSER = icu);
ALTER TEXT SEARCH CONFIGURATION icu_unaccent
  ADD MAPPING FOR word, number, kana, ideo WITH unaccent, simple;
```

### Token types

| id | alias    | ICU rule status       |
|----|----------|-----------------------|
| 2  | `word`   | `UBRK_WORD_LETTER`    |
| 12 | `blank`  | `UBRK_WORD_NONE`: spaces, punctuation, symbols |
| 22 | `number` | `UBRK_WORD_NUMBER`    |
| 24 | `kana`   | `UBRK_WORD_KANA`      |
| 25 | `ideo`   | `UBRK_WORD_IDEO`      |

The ids reuse the `default` parser's `word`, `blank` and `uint` ids, because `ts_headline` uses the built-in `prsd_headline`, which hardcodes those ids.

`number` covers any token ICU classes as numeric, which includes mixed tokens like `v1.2.3`, `1e10` and full-width `ＡＢＣ１２３`. Chinese and Japanese words found through ICU's dictionary come out as `ideo`, kana included, so map `kana` and `ideo` together.

### ICU version

`icu_parser_icu_version()` returns the version of the ICU library the server process loaded, for example `78.3`. Record it next to your indexes, since a change means they need a `REINDEX` (see below).

## Caveats

- **Segmentation depends on the ICU version.** A different ICU release can split the same text differently, the same way an ICU upgrade can change collation order. Run `REINDEX` on indexes built with this parser after PostgreSQL starts linking against a new ICU major version. Until you do, rows indexed under the old segmentation can be missed.
- **Dictionary segmentation isn't perfect for names.** ICU's dictionaries cover ordinary vocabulary. Transliterated brand names (`星巴克`, `สตาร์บัคส์`) often split into fragments. Queries go through the same parser and usually split the same way, so `plainto_tsquery` and `phraseto_tsquery` still match, but segmentation depends on context and can differ between a name on its own and the same name inside a longer string. If you need exact substring matching in these scripts, use n-gram indexes (`pg_trgm`, `pg_bigm`).
- **Prefix queries are unreliable for partial CJK words.** `to_tsquery('icu_simple', 'ラー:*')` segments `ラー` into `ラ <-> ー`, which doesn't match the single lexeme `ラーメン`. Prefixes that end on a word boundary work, as do Latin prefixes.
- **No Unicode normalization.** A decomposed `é` (`e` + U+0301) and a precomposed `é` give different lexemes. If your data mixes forms, index `to_tsvector('icu_simple', normalize(name, NFC))` and query the same way.
- **Japanese is segmented by vocabulary, not morphology.** ICU has no inflection rules, so conjugated verbs fragment: `食べました` becomes `食 / べ / ま / した`. Nouns and names, the usual search targets, segment well. For full morphological analysis, look at MeCab-based tools.
- **Different splitting for Latin text than `default`.** ICU follows Unicode word boundary rules (UAX #29): `7-Eleven` gives `7` and `eleven`, `McDonald's` stays one token, and URLs and email addresses are not recognized as single tokens.

## Performance

Measured on Wikipedia article titles, which are short, name-like strings: 500,000 titles each for English, Chinese, Japanese and Korean, and 366,332 for Thai. PostgreSQL 18.4, ICU 78.3, Apple M4 Pro, one backend, median of 5 runs. Full tables and method notes are in [bench/results.md](bench/results.md).

Time to tokenize one title, net of the table scan:

| | default parser | icu_parser | pg_trgm | pg_bigm |
|---|--:|--:|--:|--:|
| English | 0.72 µs | 0.61 µs | 1.09 µs | 0.90 µs |
| Chinese | 0.43 µs | 1.42 µs | 0.76 µs | 0.48 µs |
| Japanese | 0.57 µs | 1.70 µs | 1.00 µs | 0.65 µs |
| Korean | 0.54 µs | 0.66 µs | 0.89 µs | 0.58 µs |
| Thai | 1.08 µs | 1.67 µs | 2.17 µs | 1.42 µs |

GIN index size:

| | default parser | icu_parser | pg_trgm | pg_bigm |
|---|--:|--:|--:|--:|
| English | 25.5 MB | 24.0 MB | 29.5 MB | 20.4 MB |
| Chinese | 38.2 MB | 14.3 MB | 72.0 MB | 41.8 MB |
| Japanese | 39.6 MB | 18.6 MB | 63.7 MB | 33.9 MB |
| Korean | 30.1 MB | 28.8 MB | 48.9 MB | 24.3 MB |
| Thai | 37.6 MB | 13.7 MB | 23.4 MB | 18.8 MB |

200 single-word queries per language, terms taken from a held-out set of titles. "Found" is the share of titles containing the term as a substring that the method returned:

| | default parser | icu_parser | pg_trgm | pg_bigm |
|---|--:|--:|--:|--:|
| Chinese | 0.03 ms, 7.9% found | 0.10 ms, 78.7% found | 23.6 ms, 100% found | 0.13 ms, 100% found |
| Japanese | 0.04 ms, 5.5% found | 0.14 ms, 49.9% found | 18.6 ms, 100% found | 0.24 ms, 100% found |
| Korean | 0.07 ms, 39.0% found | 0.07 ms, 41.2% found | 15.7 ms, 100% found | 0.14 ms, 100% found |
| Thai | 0.04 ms, 1.1% found | 0.27 ms, 35.2% found | 16.6 ms, 100% found | 0.94 ms, 100% found |

What this shows:

- **The default parser can't find words in unspaced scripts.** It finds 1–8% of Chinese, Japanese and Thai occurrences, only where the word happens to be the whole run between spaces or punctuation.
- **icu_parser costs about 3× the default parser to tokenize Chinese and Japanese, 1.5× for Thai, and nothing extra for Korean or English.** That's still under 2 µs per title, and its index is the smallest for Chinese, Japanese and Thai.
- **The n-gram methods answer a different question.** `LIKE '%term%'` returns every substring occurrence, including ones that cross word boundaries (`京都` inside `東京都`). icu_parser returns word matches, which is why its share is below 100%; the gap mixes true misses with substring hits that aren't the word. Korean shows ~40% for both tokenizers because particles attach to nouns (`서울은`), and neither splits them off.
- **pg_trgm can't use its index for one- and two-character terms**, which most CJK words are, so it uses the index for only 24 of 200 Chinese queries and the rest scan. pg_bigm is the fast option when substring semantics are what you want.

Reproduce with `./bench/run.sh` (needs `curl`, `psql`, pg_trgm and optionally pg_bigm). It downloads the title dumps into `bench/data/` and writes `bench/results.md`. `BENCH_SAMPLE`, `BENCH_RUNS`, `BENCH_LANGS` and `BENCH_DUMP` change the sample size, repetitions, languages and dump date. `LC_CTYPE` must be a UTF-8 locale, or pg_trgm treats every CJK and Thai character as a separator; the script picks `C.UTF-8` when it exists.

## Tests

```sh
make installcheck
```

The regression suite in `sql/` covers segmentation across scripts, edge cases, query operators, `ts_headline`, custom configurations, GIN and GiST indexes, parallel workers, and the non-UTF-8 error. CI runs it on PostgreSQL 14 through 18.

## License

[PostgreSQL License](LICENSE)
