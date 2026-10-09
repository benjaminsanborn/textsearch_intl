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

[LANGUAGES.md](LANGUAGES.md) reports, language by language, where icu_parser works well and where it falls short.

200 single-word queries per language, each a word that icu_parser produced from a held-out set of titles, written in the language's own script. Recall and overmatch are measured on 250 term/title pairs per language, drawn at random from all titles that contain a query term as a substring, and labeled in [bench/labels.csv](bench/labels.csv) as a real word match or an overmatch (first pass by Claude, spot-checked; corrections welcome) (see [the labeling rule](bench/results.md#labeled-sample)):

| | default parser | icu_parser | pg_trgm | pg_bigm |
|---|--:|--:|--:|--:|
| Chinese | 0.03 ms, 10% recall | 0.11 ms, 93% recall, 16% overmatch | 21.7 ms, 100% recall, 18% overmatch | 0.11 ms, 100% recall, 18% overmatch |
| Japanese | 0.04 ms, 7% recall | 0.13 ms, 86% recall, 20% overmatch | 18.7 ms, 100% recall, 54% overmatch | 0.25 ms, 100% recall, 54% overmatch |
| Korean | 0.06 ms, 47% recall, 0% overmatch | 0.07 ms, 48% recall, 1% overmatch | 17.0 ms, 100% recall, 22% overmatch | 0.14 ms, 100% recall, 22% overmatch |
| Thai | 0.04 ms, 5% recall | 0.28 ms, 79% recall, 44% overmatch | 16.9 ms, 100% recall, 77% overmatch | 0.97 ms, 100% recall, 77% overmatch |

The default parser has 0% overmatch in every language. Recall rests on 204 real matches for Chinese, 115 for Japanese, 195 for Korean and 57 for Thai, so the Thai figure is good to about ±10 points.

What this shows:

- **The default parser can't find words in unspaced scripts.** It finds 5–10% of real Chinese, Japanese and Thai matches, only where the word happens to be the whole run between spaces or punctuation.
- **icu_parser finds most real matches in Chinese and Japanese.** It finds 93% in Chinese and 86% in Japanese, with far less overmatch than substring search in Japanese and Thai. Most of what it misses is the term inside a dictionary compound: a search for `機場` (airport) doesn't find `國際機場` (international airport), because ICU keeps the compound as one word.
- **Korean gains nothing from icu_parser.** Words are already separated by spaces, but particles and suffixes attach to nouns (`서울은`, `스타벅스커피`), and neither tokenizer splits them off. For Korean, use n-grams.
- **ICU breaks up names it doesn't know.** Unfamiliar transliterated names come out as fragments that are real tokens (`อง`, `ซ์` in Thai; `ンズ`, `ジョ` in Japanese). Those fragments match inside unrelated names, which is where icu_parser's Thai and Japanese overmatch comes from. It also biases this benchmark: the query terms are drawn from icu_parser's own output, so they include such fragments, which nobody would search for.
- **The n-gram methods return every substring.** They score 100% recall by definition, and the overmatch column shows what that costs.
- **pg_trgm can't use its index for one- and two-character terms**, which most CJK words are. It used the index for only 22 of 200 Chinese queries, and the rest scanned the table. pg_bigm gives the same results at index speed.
- **Cost:** icu_parser takes about 3× the default parser's time to tokenize Chinese and Japanese, 1.5× for Thai, and about the same for Korean and English. That's still under 2 µs per title, and its index is the smallest for Chinese, Japanese and Thai.

Reproduce with `./bench/run.sh` (needs `curl`, `psql`, pg_trgm and optionally pg_bigm). It downloads the title dumps into `bench/data/`, scores every method against `bench/labels.csv`, and writes `bench/results.md`. To label a fresh sample, for example after changing the dump or the term rules, run `psql -d icu_parser_bench -v per_lang=250 -f bench/pairs.sql > pairs.csv` after a run, add a `label` column, and save it as `bench/labels.csv`. `BENCH_SAMPLE`, `BENCH_RUNS`, `BENCH_LANGS` and `BENCH_DUMP` change the sample size, repetitions, languages and dump date. `LC_CTYPE` must be a UTF-8 locale, or pg_trgm treats every CJK and Thai character as a separator; the script picks `C.UTF-8` when it exists.

## Tests

```sh
make installcheck
```

The regression suite in `sql/` covers segmentation across scripts, edge cases, query operators, `ts_headline`, custom configurations, GIN and GiST indexes, parallel workers, and the non-UTF-8 error. CI runs it on PostgreSQL 14 through 18.

## License

[PostgreSQL License](LICENSE)
