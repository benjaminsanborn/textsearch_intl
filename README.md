# textsearch_intl

Multilingual full text search for PostgreSQL, built for names and other short text in any script. It provides:

- **the `intl` parser,** which splits text into words with ICU word boundaries, including Chinese, Japanese, Thai, Khmer, Lao and Burmese, which don't put spaces between words;
- **the `intl_normalize` dictionary,** which folds character widths, decomposed accents and apostrophes;
- **the `intl_ngram` dictionary,** which lets a search match words inside long Korean, Chinese and Japanese tokens;
- **three ready-made configurations** that combine them.

PostgreSQL's built-in `default` parser only splits on spaces and punctuation, so text in scripts written without spaces between words comes out as a single token. Chinese, Japanese, Thai, Khmer, Lao and Burmese all have this problem:

```sql
SELECT token FROM ts_debug('simple', '東京駅前ラーメン店');
--  東京駅前ラーメン店
```

The `intl` parser uses ICU's word break iterator, which uses dictionaries to segment those scripts:

```sql
CREATE EXTENSION textsearch_intl CASCADE;

SELECT alias, token FROM ts_debug('intl_simple', '東京駅前ラーメン店 ร้านกาแฟ Café');
--  ideo  | 東京
--  ideo  | 駅前
--  ideo  | ラーメン
--  ideo  | 店
--  blank |
--  word  | ร้าน
--  word  | กาแฟ
--  blank |
--  word  | Café

SELECT to_tsvector('intl_simple', '東京駅前ラーメン店') @@ plainto_tsquery('intl_simple', 'ラーメン');
--  t
```

## Requirements

- PostgreSQL 14 or later, in a `UTF8` database
- ICU development headers (`libicu-dev`, `libicu-devel` or Homebrew `icu4c`) and `pkg-config`
- The `unaccent` extension from contrib, which `CREATE EXTENSION textsearch_intl CASCADE` installs

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

The extension creates the parser `intl`, the dictionaries `intl_normalize` and `intl_ngram`, and three configurations:

| Configuration | Dictionaries for each word | Use it for |
|---|---|---|
| `intl_search_ngram` | `intl_normalize`, `intl_ngram`, `unaccent`, `simple` | Searching names in any language, including Korean and words inside CJK compounds |
| `intl_search` | `intl_normalize`, `unaccent`, `simple` | Whole-word search; the smallest index, and no matches inside longer Korean or CJK words |
| `intl_simple` | `simple` | Words exactly as written, keeping accents, apostrophes and character widths |

```sql
CREATE INDEX places_name_intl_idx ON places USING gin (to_tsvector('intl_search_ngram', name));

SELECT * FROM places
WHERE to_tsvector('intl_search_ngram', name) @@ phraseto_tsquery('intl_search_ngram', 'mcdonalds');
-- finds McDonald's, McDonald’s and MCDONALDS

SELECT * FROM places
WHERE to_tsvector('intl_search_ngram', name) @@ phraseto_tsquery('intl_search_ngram', '편의점');
-- finds 행복한편의점 and CU편의점 as well as 편의점
```

Prefer `phraseto_tsquery`: when ICU splits a name into several words, `plainto_tsquery` matches them in any order.

### Normalization

`intl_normalize` is a filtering dictionary: it changes a token and passes it on to the next dictionary, or passes it through untouched. For each token it:

- applies Unicode NFKC, so half-width katakana (`ｾﾌﾞﾝ` → `セブン`), full-width letters and digits (`ＡＢＣ１２３` → `ABC123`) and decomposed accents match their usual forms;
- recombines the Thai `ำ` and Lao `ຳ` vowels, which NFKC splits into two characters;
- removes apostrophes (`'` and `’`), so `McDonald's` becomes `McDonalds`.

`intl_search` then strips Latin, Greek and Cyrillic accents with `unaccent` (`café` → `cafe`, `ё` → `е`) and lowercases with `simple`. `unaccent` leaves Thai tone marks and Indic vowel signs alone, which a generic "remove all combining marks" step would break. `ts_headline` still shows the original text.

The dictionary works on the tokens ICU produced, so it can't join words ICU split: `7eleven` and `7-Eleven` still differ.

### Matching inside Korean and CJK words

ICU keeps some long runs as a single token: Korean puts particles and branch names directly onto a word (`서울의`, `스타벅스안양평촌점`), and ICU's Chinese and Japanese dictionaries keep compounds whole (`中村公園店`, `駅前`). A whole-word search for `스타벅스`, `公園` or `駅` misses those.

For tokens made entirely of Han, kana or Hangul, `intl_ngram` indexes every character and every pair of adjacent characters, all at the token's position. A query goes through the same expansion, and `to_tsquery` ANDs lexemes that share a position:

```sql
SELECT phraseto_tsquery('intl_search_ngram', '스타벅스 안양');
--  ( '스' & '타' & '벅' & '스' & '스타' & '타벅' & '벅스' ) <-> ( '안' & '양' & '안양' )
```

So inside a single token, a search matches the way `LIKE '%…%'` does, and phrase operators still enforce word order between tokens. ICU's word boundaries still apply: `京都` doesn't match `東京|都`. The price is overmatch inside a token, the same kind substring search has everywhere: `스타벅스` also matches `파스타벅스` (a pasta restaurant), and `主教` (bishop) matches `天主教` (Catholicism). Pieces only need to appear somewhere in the token, not necessarily adjacent, so a long token can occasionally match a query whose characters it contains out of order. Tokens in other scripts pass through unchanged.

Build your own configuration on the parser to add stemming or stop words:

```sql
CREATE TEXT SEARCH CONFIGURATION intl_english (PARSER = intl);
ALTER TEXT SEARCH CONFIGURATION intl_english
  ADD MAPPING FOR word WITH intl_normalize, unaccent, english_stem;
ALTER TEXT SEARCH CONFIGURATION intl_english
  ADD MAPPING FOR number, kana, ideo WITH intl_normalize, simple;
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

`intl_icu_version()` returns the version of the ICU library the server process loaded, for example `78.3`. Record it next to your indexes, since a change means they need a `REINDEX` (see below).

## Caveats

- **Segmentation depends on the ICU version.** A different ICU release can split the same text differently, the same way an ICU upgrade can change collation order. Run `REINDEX` on indexes built with this parser after PostgreSQL starts linking against a new ICU major version. Until you do, rows indexed under the old segmentation can be missed.
- **Dictionary segmentation isn't perfect for names.** ICU's dictionaries cover ordinary vocabulary. Transliterated brand names (`星巴克`, `สตาร์บัคส์`) often split into fragments. Queries go through the same parser and usually split the same way, so `plainto_tsquery` and `phraseto_tsquery` still match, but segmentation depends on context and can differ between a name on its own and the same name inside a longer string. If you need exact substring matching in these scripts, use n-gram indexes (`pg_trgm`, `pg_bigm`).
- **Prefix queries are unreliable for partial CJK words.** `to_tsquery('intl_simple', 'ラー:*')` segments `ラー` into `ラ <-> ー`, which doesn't match the single lexeme `ラーメン`. Prefixes that end on a word boundary work, as do Latin prefixes.
- **`intl_simple` doesn't normalize.** A decomposed `é` (`e` + U+0301) and a precomposed `é` give different lexemes, as do half-width and full-width forms. Use `intl_search`, or put `intl_normalize` in your own configuration.
- **Japanese is segmented by vocabulary, not morphology.** ICU has no inflection rules, so conjugated verbs fragment: `食べました` becomes `食 / べ / ま / した`. Nouns and names, the usual search targets, segment well. For full morphological analysis, look at MeCab-based tools.
- **Different splitting for Latin text than `default`.** ICU follows Unicode word boundary rules (UAX #29): `7-Eleven` gives `7` and `eleven`, `McDonald's` stays one token, and URLs and email addresses are not recognized as single tokens.

## Performance

Measured on Wikipedia article titles, which are short, name-like strings: 500,000 titles each for English, Chinese, Japanese and Korean, and 366,332 for Thai. PostgreSQL 18.4, ICU 78.3, Apple M4 Pro, one backend, median of 5 runs. Full tables and method notes, including `intl_simple`, are in [bench/results.md](bench/results.md).

Time to tokenize one title, net of the table scan:

| | default parser | intl_search | intl_search_ngram | pg_trgm | pg_bigm |
|---|--:|--:|--:|--:|--:|
| English | 0.77 µs | 0.84 µs | 0.90 µs | 1.20 µs | 0.99 µs |
| Chinese | 0.47 µs | 1.86 µs | 2.12 µs | 0.82 µs | 0.52 µs |
| Japanese | 0.62 µs | 2.18 µs | 2.63 µs | 1.09 µs | 0.72 µs |
| Korean | 0.59 µs | 0.89 µs | 1.26 µs | 0.96 µs | 0.62 µs |
| Thai | 1.17 µs | 2.25 µs | 2.33 µs | 2.33 µs | 1.56 µs |

GIN index size:

| | default parser | intl_search | intl_search_ngram | pg_trgm | pg_bigm |
|---|--:|--:|--:|--:|--:|
| English | 25.5 MB | 23.4 MB | 23.5 MB | 29.5 MB | 20.4 MB |
| Chinese | 38.2 MB | 14.3 MB | 18.2 MB | 72.0 MB | 41.8 MB |
| Japanese | 39.6 MB | 18.6 MB | 20.7 MB | 63.7 MB | 33.9 MB |
| Korean | 30.1 MB | 28.8 MB | 24.6 MB | 48.9 MB | 24.3 MB |
| Thai | 37.6 MB | 13.7 MB | 13.7 MB | 23.4 MB | 18.8 MB |

[LANGUAGES.md](LANGUAGES.md) reports, language by language, where textsearch_intl works well and where it falls short.

200 single-word queries per language, each a word that `intl_simple` produced from a held-out set of titles, written in the language's own script. Recall and overmatch are measured on 250 term/title pairs per language, drawn at random from all titles that contain a query term as a substring, and labeled in [bench/labels.csv](bench/labels.csv) as a real word match or an overmatch (first pass by Claude, spot-checked; corrections welcome) (see [the labeling rule](bench/results.md#labeled-sample)). Average time per query, recall, overmatch:

| | default parser | intl_search | intl_search_ngram | pg_trgm | pg_bigm |
|---|--:|--:|--:|--:|--:|
| Chinese | 0.04 ms, 10%, 0% | 0.14 ms, 93%, 16% | 0.23 ms, 99%, 18% | 23.4 ms, 100%, 18% | 0.11 ms, 100%, 18% |
| Japanese | 0.04 ms, 7%, 0% | 0.13 ms, 86%, 20% | 0.50 ms, 98%, 53% | 19.6 ms, 100%, 54% | 0.26 ms, 100%, 54% |
| Korean | 0.07 ms, 47%, 0% | 0.09 ms, 48%, 1% | 0.44 ms, 100%, 22% | 17.9 ms, 100%, 22% | 0.14 ms, 100%, 22% |
| Thai | 0.04 ms, 5%, 0% | 0.29 ms, 79%, 44% | 0.28 ms, 79%, 44% | 17.1 ms, 100%, 77% | 0.98 ms, 100%, 77% |

Recall rests on 204 real matches for Chinese, 115 for Japanese, 195 for Korean and 57 for Thai, so the Thai figure is good to about ±10 points.

What this shows:

- **The default parser can't find words in unspaced scripts.** It finds 5–10% of real Chinese, Japanese and Thai matches, only where the word happens to be the whole run between spaces or punctuation.
- **intl_search finds most real matches in Chinese and Japanese**, 93% and 86%, with far less overmatch than substring search in Japanese and Thai. What it misses is the term inside a dictionary compound: `機場` (airport) doesn't find `國際機場` (international airport), because ICU keeps the compound as one word.
- **intl_search does nothing for Korean.** Particles and suffixes attach to nouns (`서울은`, `스타벅스커피`), and word segmentation doesn't split them off.
- **intl_search_ngram closes those gaps.** Korean recall goes from 48% to 100%, Chinese from 93% to 99% and Japanese from 86% to 98%. In exchange it overmatches like the substring methods do, because inside Korean and CJK words it is a substring match. It leaves Thai, English and other scripts exactly as intl_search does.
- **ICU breaks up names it doesn't know.** Unfamiliar transliterated names come out as fragments that are real tokens (`อง`, `ซ์` in Thai; `ンズ`, `ジョ` in Japanese). Those fragments match inside unrelated names, which is where intl_search's Thai and Japanese overmatch comes from. It also biases this benchmark: the query terms are drawn from the tokenizer's own output, so they include such fragments, which nobody would search for.
- **The n-gram methods return every substring.** They score 100% recall by definition, and the overmatch column shows what that costs.
- **pg_trgm can't use its index for one- and two-character terms**, which most CJK words are. It used the index for only 22 of 200 Chinese queries, and the rest scanned the table. pg_bigm gives the same results at index speed.
- **Cost:** intl_search takes 2–4× the default parser's time to tokenize Chinese, Japanese and Thai, and intl_search_ngram a little more, but both stay under 3 µs per title. Their indexes are the smallest for Chinese, Japanese and Thai, and intl_search_ngram's Korean index is the size of pg_bigm's.

Reproduce with `./bench/run.sh` (needs `curl`, `psql`, pg_trgm and optionally pg_bigm). It downloads the title dumps into `bench/data/`, scores every method against `bench/labels.csv`, and writes `bench/results.md`. To label a fresh sample, for example after changing the dump or the term rules, run `psql -d textsearch_intl_bench -v per_lang=250 -f bench/pairs.sql > pairs.csv` after a run, add a `label` column, and save it as `bench/labels.csv`. `BENCH_SAMPLE`, `BENCH_RUNS`, `BENCH_LANGS` and `BENCH_DUMP` change the sample size, repetitions, languages and dump date. `LC_CTYPE` must be a UTF-8 locale, or pg_trgm treats every CJK and Thai character as a separator; the script picks `C.UTF-8` when it exists.

## Tests

```sh
make installcheck
```

The regression suite in `sql/` covers segmentation across scripts, edge cases, query operators, `ts_headline`, custom configurations, GIN and GiST indexes, parallel workers, and the non-UTF-8 error. CI runs it on PostgreSQL 14 through 18.

## License

[PostgreSQL License](LICENSE)
