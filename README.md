# textsearch_intl

Multilingual full text search for PostgreSQL, built for names and other short text in any script. It provides:

- **the `intl` parser,** which splits text into words with ICU word boundaries, including Chinese, Japanese, Thai, Khmer, Lao and Burmese, which don't put spaces between words;
- **the `intl_normalize` dictionary,** which folds character widths, decomposed accents and apostrophes;
- **the `intl_runs` parser,** the same as `intl` except that it keeps each unspaced Thai, Lao, Khmer or Burmese run as one token;
- **the `intl_ngram` dictionary,** which lets a search match words inside long Korean, Chinese and Japanese tokens and inside Thai, Lao, Khmer and Burmese runs;
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

The extension creates the parsers `intl` and `intl_runs`, the dictionaries `intl_normalize` and `intl_ngram`, and three configurations:

| Configuration | Parser | Dictionaries for each word | Use it for |
|---|---|---|---|
| `intl_search_ngram` | `intl_runs` | `intl_normalize`, `intl_ngram`, `unaccent`, `simple` | Searching names in any language, including Korean, words inside CJK compounds, and Thai, Lao, Khmer and Burmese names ICU doesn't know |
| `intl_search` | `intl` | `intl_normalize`, `unaccent`, `simple` | Whole-word search; the smallest index, and no matches inside longer Korean or CJK words |
| `intl_simple` | `intl` | `simple` | Words exactly as written, keeping accents, apostrophes and character widths |

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

- applies Unicode NFKC with case folding, so half-width katakana (`ｾﾌﾞﾝ` → `セブン`), full-width letters and digits (`ＡＢＣ１２３` → `abc123`) and decomposed accents match their usual forms, and case matches without depending on the database locale: `ΑΘΗΝΑΣ` matches `Αθήνας` (final sigma), `STRASSE` matches `Straße`, and `İstanbul` becomes `istanbul`;
- drops invisible characters such as soft hyphens, zero-width joiners and variation selectors;
- recombines the Thai `ำ` and Lao `ຳ` vowels, which NFKC splits into two characters;
- removes apostrophes and the letters and accents typed in their place (`'`, `’`, `‘`, `ʻ`, `ʼ`, `´`, `` ` ``, `′`), so `McDonald's` becomes `mcdonalds`, `Oʻahu` becomes `oahu` and `Пʼятниця` becomes `пятниця`;
- folds Arabic and Persian letter variants that people type interchangeably in names (alef forms, `ى`/`ی` → `ي`, `ک` → `ك`, `ة` → `ه`) and removes short-vowel marks and tatweel, so `أحمد`, `احمد` and `مُحَمَّد`/`محمد` match;
- maps every decimal digit to ASCII, so `٧`, `۷` and `๗` match `7`.

A word with an elided French or Italian article or pronoun (`L'Occitane`, `l’École`, `dell'Arte`) or the Arabic article (`المطعم`, `والمطعم`) gets two lexemes at its position, the whole word and the word without the article, so a search for either `L'Occitane` or `occitane`, `المطعم` or `مطعم`, finds it. A filtering dictionary can only pass one lexeme on, so for these words `intl_normalize` returns the final lexemes itself, already accent-folded like `unaccent` would, and later dictionaries don't see them. A search that includes the article still needs it in the name: `المطعم` doesn't find a name written `مطعم`.

`intl_search` then strips Latin, Greek and Cyrillic accents with `unaccent` (`café` → `cafe`, `ё` → `е`) and lowercases with `simple`. `unaccent` leaves Thai tone marks and Indic vowel signs alone, which a generic "remove all combining marks" step would break. `ts_headline` still shows the original text.

The dictionary works on the tokens ICU produced, so it can't join words ICU split: `7eleven` and `7-Eleven` still differ.

### Matching inside words

ICU keeps some long runs as a single token: Korean puts particles and branch names directly onto a word (`서울의`, `스타벅스안양평촌점`), and ICU's Chinese and Japanese dictionaries keep compounds whole (`中村公園店`, `駅前`). A whole-word search for `스타벅스`, `公園` or `駅` misses those.

For tokens made entirely of Han, kana or Hangul, `intl_ngram` indexes every character and every pair of adjacent characters, all at the token's position. A query goes through the same expansion, and `to_tsquery` ANDs lexemes that share a position:

```sql
SELECT phraseto_tsquery('intl_search_ngram', '스타벅스 안양');
--  ( '스' & '타' & '벅' & '스' & '스타' & '타벅' & '벅스' ) <-> ( '안' & '양' & '안양' )
```

So inside a single token, a search matches the way `LIKE '%…%'` does, and phrase operators still enforce word order between tokens. ICU's word boundaries still apply: `京都` doesn't match `東京|都`. The price is overmatch inside a token, the same kind substring search has everywhere: `스타벅스` also matches `파스타벅스` (a pasta restaurant), and `主教` (bishop) matches `天主教` (Catholicism). Pieces only need to appear somewhere in the token, not necessarily adjacent, so a long token can occasionally match a query whose characters it contains out of order. 
Thai, Lao, Khmer and Burmese have the opposite problem: ICU splits names it doesn't know at the wrong places. `เซเว่นอีเลฟเว่น` (7-Eleven) comes out as `เซ|เว่|นอีเลฟเว่น`, so `เซเว่น` never appears as a token. The `intl_runs` parser therefore keeps each unspaced run of those scripts as one token, and `intl_ngram` turns it into every pair and every three adjacent grapheme clusters, so vowel and tone marks stay with their consonant:

```sql
SELECT ts_lexize('intl_ngram', 'กาแฟ');
--  {กา,าแ,แฟ,กาแ,าแฟ}

SELECT to_tsvector('intl_search_ngram', 'เซเว่นอีเลฟเว่น') @@ phraseto_tsquery('intl_search_ngram', 'เซเว่น');
--  t
```

A search then matches anywhere inside a run, like `pg_trgm` does, and a single-cluster search only matches a run that is exactly that cluster. Words separated by spaces are still separate tokens for phrase queries. A run mixed with other scripts (`กาแฟcoffee`), and tokens in other scripts, pass through unchanged.

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
| English | 0.69 µs | 0.80 µs | 0.87 µs | 1.11 µs | 0.88 µs |
| Chinese | 0.42 µs | 1.73 µs | 1.93 µs | 0.75 µs | 0.47 µs |
| Japanese | 0.59 µs | 2.09 µs | 2.52 µs | 1.09 µs | 0.68 µs |
| Korean | 0.57 µs | 0.90 µs | 1.28 µs | 0.93 µs | 0.60 µs |
| Thai | 1.14 µs | 2.26 µs | 3.44 µs | 2.29 µs | 1.51 µs |

GIN index size:

| | default parser | intl_search | intl_search_ngram | pg_trgm | pg_bigm |
|---|--:|--:|--:|--:|--:|
| English | 25.5 MB | 23.5 MB | 23.6 MB | 29.5 MB | 20.4 MB |
| Chinese | 38.2 MB | 14.3 MB | 18.2 MB | 72.0 MB | 41.8 MB |
| Japanese | 39.6 MB | 18.6 MB | 20.7 MB | 63.7 MB | 33.9 MB |
| Korean | 30.1 MB | 28.8 MB | 24.6 MB | 48.9 MB | 24.3 MB |
| Thai | 37.6 MB | 13.7 MB | 45.5 MB | 23.4 MB | 18.8 MB |

[LANGUAGES.md](LANGUAGES.md) reports, language by language, where textsearch_intl works well and where it falls short.

200 single-word queries per language, each a word that `intl_simple` produced from a held-out set of titles, written in the language's own script. Recall and overmatch are measured on 250 term/title pairs per language, drawn at random from all titles that contain a query term as a substring, and labeled in [bench/labels.csv](bench/labels.csv) as a real word match or an overmatch (first pass by Claude, spot-checked; corrections welcome) (see [the labeling rule](bench/results.md#labeled-sample)). Average time per query, recall, overmatch:

| | default parser | intl_search | intl_search_ngram | pg_trgm | pg_bigm |
|---|--:|--:|--:|--:|--:|
| Chinese | 0.03 ms, 10%, 0% | 0.10 ms, 93%, 16% | 0.18 ms, 99%, 18% | 23.4 ms, 100%, 18% | 0.14 ms, 100%, 18% |
| Japanese | 0.04 ms, 7%, 0% | 0.17 ms, 86%, 20% | 0.54 ms, 98%, 53% | 20.1 ms, 100%, 54% | 0.33 ms, 100%, 54% |
| Korean | 0.08 ms, 47%, 0% | 0.09 ms, 48%, 1% | 0.41 ms, 100%, 22% | 18.3 ms, 100%, 22% | 0.18 ms, 100%, 22% |
| Thai | 0.04 ms, 5%, 0% | 0.38 ms, 79%, 44% | 0.67 ms, 98%, 69% | 17.7 ms, 100%, 77% | 1.26 ms, 100%, 77% |

Recall rests on 204 real matches for Chinese, 115 for Japanese, 195 for Korean and 57 for Thai, so the Thai figure is good to about ±10 points.

What this shows:

- **The default parser can't find words in unspaced scripts.** It finds 5–10% of real Chinese, Japanese and Thai matches, only where the word happens to be the whole run between spaces or punctuation.
- **intl_search finds most real matches in Chinese and Japanese**, 93% and 86%, with far less overmatch than substring search in Japanese and Thai. What it misses is the term inside a dictionary compound: `機場` (airport) doesn't find `國際機場` (international airport), because ICU keeps the compound as one word.
- **intl_search does nothing for Korean.** Particles and suffixes attach to nouns (`서울은`, `스타벅스커피`), and word segmentation doesn't split them off.
- **intl_search_ngram closes those gaps.** Korean recall goes from 48% to 100%, Chinese from 93% to 99%, Japanese from 86% to 98% and Thai from 79% to 98%. In exchange it overmatches like the substring methods do, because inside Korean, CJK and Thai runs it is a substring match. It leaves English and other spaced scripts exactly as intl_search does.
- **ICU breaks up names it doesn't know.** Unfamiliar transliterated names come out as fragments that are real tokens (`อง`, `ซ์` in Thai; `ンズ`, `ジョ` in Japanese). Those fragments match inside unrelated names, which is where intl_search's Thai and Japanese overmatch comes from. It also biases this benchmark: the query terms are drawn from the tokenizer's own output, so they include such fragments, which nobody would search for.
- **The n-gram methods return every substring.** They score 100% recall by definition, and the overmatch column shows what that costs.
- **pg_trgm can't use its index for one- and two-character terms**, which most CJK words are. It used the index for only 22 of 200 Chinese queries, and the rest scanned the table. pg_bigm gives the same results at index speed.
- **Cost:** intl_search takes 2–4× the default parser's time to tokenize Chinese, Japanese and Thai, and intl_search_ngram a little more (3.4 µs per Thai title). intl_search's indexes are the smallest for Chinese, Japanese and Thai. intl_search_ngram's are smaller than pg_bigm's for Chinese and Japanese and the same size for Korean, but for Thai, whose runs get both bigrams and trigrams, it is about twice the size of pg_trgm's.

Reproduce with `./bench/run.sh` (needs `curl`, `psql`, pg_trgm and optionally pg_bigm). It downloads the title dumps into `bench/data/`, scores every method against `bench/labels.csv`, and writes `bench/results.md`. To label a fresh sample, for example after changing the dump or the term rules, run `psql -d textsearch_intl_bench -v per_lang=250 -f bench/pairs.sql > pairs.csv` after a run, add a `label` column, and save it as `bench/labels.csv`. `BENCH_SAMPLE`, `BENCH_RUNS`, `BENCH_LANGS` and `BENCH_DUMP` change the sample size, repetitions, languages and dump date. `LC_CTYPE` must be a UTF-8 locale, or pg_trgm treats every CJK and Thai character as a separator; the script picks `C.UTF-8` when it exists.

## Tests

```sh
make installcheck
```

The regression suite in `sql/` covers segmentation across scripts, edge cases, query operators, `ts_headline`, custom configurations, GIN and GiST indexes, parallel workers, and the non-UTF-8 error. CI runs it on PostgreSQL 14 through 18.

## License

[PostgreSQL License](LICENSE)
