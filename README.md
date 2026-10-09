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

## Caveats

- **Segmentation depends on the ICU version.** A different ICU release can split the same text differently, the same way an ICU upgrade can change collation order. Run `REINDEX` on indexes built with this parser after PostgreSQL starts linking against a new ICU major version. Until you do, rows indexed under the old segmentation can be missed.
- **Dictionary segmentation isn't perfect for names.** ICU's dictionaries cover ordinary vocabulary. Transliterated brand names (`星巴克`, `สตาร์บัคส์`) often split into fragments. Queries go through the same parser and usually split the same way, so `plainto_tsquery` and `phraseto_tsquery` still match, but segmentation depends on context and can differ between a name on its own and the same name inside a longer string. If you need exact substring matching in these scripts, use n-gram indexes (`pg_trgm`, `pg_bigm`).
- **Different splitting for Latin text than `default`.** ICU follows Unicode word boundary rules (UAX #29): `7-Eleven` gives `7` and `eleven`, `McDonald's` stays one token, and URLs and email addresses are not recognized as single tokens.

## License

[PostgreSQL License](LICENSE)
