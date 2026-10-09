# How icu_parser performs by language

icu_parser splits text with ICU's word-break rules. For scripts written without spaces (Chinese, Japanese, Thai, Lao, Khmer, Burmese), ICU looks words up in its dictionaries; everywhere else it splits on spaces and punctuation like the default parser does. So it helps most where the default parser fails completely, and it does nothing for problems that come from word *forms*: particles, clitics, compounds and inflection.

The evidence comes from three places:

- **Benchmark:** 500,000 Wikipedia titles per language, with recall and overmatch scored on 250 labeled term/title pairs per language ([bench/results.md](bench/results.md)). Recall is the share of real word matches a method returns; overmatch is the share of its results that aren't real matches.
- **Production place names:** single-word searches against a table of about 550 million business and place names, comparing icu_parser with the default parser (`simple`) and a pg_trgm substring index.
- **Examples:** every tokenization below was produced by icu_parser on ICU 78.3. Another ICU version can split some dictionary words differently.

## Summary

| Language | Verdict | Benchmark recall (default → icu_parser) | icu_parser overmatch | Main weakness |
|---|---|--:|--:|---|
| Chinese | **Good** | 10% → 93% | 16% | Transliterated names split into single characters; some compounds stay whole |
| Japanese | **Good for kanji and katakana** | 7% → 86% | 20% | Hiragana words split into syllables; compounds; half-width kana |
| Thai | **Usable** | 5% → 79% | 44% | Unknown names and loanwords break unpredictably |
| Lao, Khmer, Burmese | Likely good (not benchmarked) | | | Same dictionary approach as Thai |
| Korean | **No gain** | 47% → 48% | 1% | Particles and compound names stay attached |
| Vietnamese | No gain | | | Words span several space-separated syllables |
| Arabic, Hebrew | No gain | | | Articles and prepositions are attached to words |
| English and other spaced languages | Same as default | | | No stemming or compound splitting |

## Advice that applies to every language

- **Query with `phraseto_tsquery`, not `plainto_tsquery`.** When ICU splits a name into pieces, a plain query requires the pieces to appear anywhere in the title, in any order. `星巴克` (Starbucks) splits into `星|巴|克`, so `plainto_tsquery` matches `克巴星餐厅`; `phraseto_tsquery` requires them adjacent and in order, and doesn't.
- **Normalize before tokenizing.** icu_parser doesn't apply Unicode normalization. Index `to_tsvector('icu_simple', normalize(name, NFKC))` so that half-width katakana (`ｾﾌﾞﾝ`), full-width Latin (`ＡＢＣ`) and decomposed accents match their usual forms.
- **Strip accents with a config if your users type without them.** The `unaccent` dictionary works with icu_parser (see the README's custom configuration example).
- **Pair it with an n-gram index for partial words.** icu_parser matches whole words only. For Korean, for names ICU doesn't know, and for prefixes of CJK words, add pg_bigm (or pg_trgm for terms of three or more characters) and OR the two conditions.

## Chinese: good

The biggest improvement of any language. On Wikipedia titles, recall goes from 10% with the default parser to 93%, at 16% overmatch against 18% for substring search. On place names, single-word searches for common words found 100–1,200 times more rows than the default parser:

| Search | default parser | icu_parser | pg_trgm |
|---|--:|--:|--:|
| 公園 (park) | 315 | 116,892 | timed out |
| 銀行 (bank) | 45 | 54,071 | timed out |
| 機場 (airport) | 44 | 5,027 | timed out |
| 麥當勞 (McDonald's) | 680 | 722 | 722 |
| 星巴克 (Starbucks) | 922 | 1,169 | 1,173 |

pg_trgm can't use its index for one- and two-character terms, so those searches read the whole index and timed out at 20 seconds.

Where it falls short:

- **Transliterated foreign names** aren't in ICU's dictionary and come out as single characters: `朱莉娅·加布里埃莱斯基` → `朱|莉|娅|加布里|埃|莱|斯|基`, and `星巴克咖啡` → `星|巴|克|咖啡`. A phrase query still finds them, but a plain query overmatches.
- **Some compounds stay whole.** `國際機場` (international airport, Traditional) is one token, so a search for `機場` misses it, while the Simplified `国际机场` splits into `国际|机场`. This is most of the 7% the benchmark missed.
- **Traditional and Simplified characters are different text.** `麥當勞` and `麦当劳` don't match each other. Convert one way before indexing if your data mixes them.

## Japanese: good for kanji and katakana, weak for hiragana

Recall goes from 7% to 86%, with 20% overmatch against 54% for substring search. Kanji compounds split well (`東京国際空港` → `東京|国際|空港`, so a search for `空港` finds it). Katakana splits at hyphens and middle dots: `セブン-イレブン渋谷店` → `セブン|イレブン|渋谷|店`, so a search for `セブンイレブン` finds the hyphenated form. On place names that found 18,268 rows, against 541 for a substring search that expected no hyphen.

Where it falls short:

- **Hiragana-only words split into syllables:** `ひらがな` → `ひ|ら|が|な`, `やきとり` → `や|き|とり`, `らーめん` → `ら|ー|めん`. Verb forms fragment the same way: `食べました` → `食|べ|ま|した`. Use phrase queries; matching across spelling variants (`らーめん` and `ラーメン`) needs a kana-folding step that icu_parser doesn't do.
- **Unknown katakana names** can come out as fragments (`ンズ`, `ジョ`), which then match inside unrelated names. This is most of the 20% overmatch.
- **Half-width katakana** (`ｾﾌﾞﾝｲﾚﾌﾞﾝ`) doesn't match full-width until it's normalized with NFKC.
- **No morphology.** ICU isn't a morphological analyzer like MeCab or Kuromoji, so it won't link inflected forms or break up every compound.

## Thai: usable, with noisy unknown words

Recall goes from 5% to 79%, at 44% overmatch against 77% for substring search. Dictionary words split cleanly: `ร้านกาแฟสด` → `ร้าน|กาแฟ|สด`, `โรงพยาบาลกรุงเทพ` → `โรง|พยาบาล|กรุงเทพ`. On place names, icu_parser found 39,019 rows for `กาแฟ` (coffee) against 3,808 for the default parser and 40,492 for substring search.

Where it falls short:

- **Unknown names pull letters off the next word.** `กุ๊งกิ๊งกาแฟโบราณ` → `กุ๊|งกิ๊|งกาแฟ|โบราณ`: the name isn't in the dictionary, so `กาแฟ` picks up its last letter and a search for it misses.
- **Loanwords split differently depending on what follows them.** `เซเว่น` (Seven) on its own is `เซ|เว่น`, but in `เซเว่นอีเลฟเว่น` (7-Eleven) it's `เซ|เว่|นอีเลฟเว่น`, so a search for `เซเว่น` doesn't find the full brand name. On place names, icu_parser found 2,094 rows against 3,794 for substring search.
- **Thai followed directly by Latin letters stays joined:** `ร้านกาแฟcoffee` → `ร้าน|กาแฟcoffee`.
- **Fragments overmatch.** Short fragments like `อง` and `ซ์` come out of unknown names as tokens and match inside unrelated words.

## Lao, Khmer, Burmese: likely good, not benchmarked

ICU has dictionaries for all three, and sentences split into words: `ຮ້ານອາຫານລາວ` → `ຮ້ານ|ອາຫານ|ລາວ`, `ភោជនីយដ្ឋានខ្មែរ` → `ភោជនីយដ្ឋាន|ខ្មែរ`, `မြန်မာစားသောက်ဆိုင်` → `မြန်မာ|စားသောက်ဆိုင်`. Expect the same weaknesses as Thai with unknown names.

## Korean: no gain

Korean puts spaces between phrases, but attaches particles, endings and other nouns directly to a word. ICU has no Korean dictionary, so each space-separated run is one token, exactly as with the default parser. Recall was 48% against 47% for the default parser.

- **Particles:** `서울의 맛집` → `서울의|맛집`, so a search for `서울` doesn't match.
- **Brand plus branch names:** `스타벅스안양평촌점` (Starbucks, Anyang Pyeongchon branch) is a single token. On place names, a search for `편의점` (convenience store) found 2,041 rows with icu_parser and 9,096 with substring search, because names like `행복한편의점` are one word.

Use pg_bigm or pg_trgm for Korean, or a morphological analyzer outside Postgres (such as Elasticsearch's nori).

## Vietnamese: no gain

Vietnamese writes each syllable separately, and most words are two or more syllables. ICU splits at every space, like the default parser: `Nhà hàng Phở Hà Nội` → `Nhà|hàng|Phở|Hà|Nội`. Use `phraseto_tsquery` so `Hà Nội` matches as a unit, and the `unaccent` dictionary if users type without diacritics.

## Arabic and Hebrew: no gain

Spaces separate words, but the article and some prepositions and conjunctions are written as part of the word. `المطعم` (the restaurant) is one token, so a search for `مطعم` (restaurant) misses it, and `وبالمطعم` (and in the restaurant) is one token too. Hebrew does the same with `המסעדה` against `מסעדה`. Neither icu_parser nor the default parser removes these prefixes.

## English and other spaced languages: same as the default parser

For English, Russian, Greek, Hindi, German, Turkish and other languages that separate words with spaces, ICU's splits match the default parser's, and tokenizing English is slightly faster (0.63 µs against 0.73 µs per title). Search results were the same within 0.5%: `starbucks` found 54,395 place names with icu_parser and 54,352 with the default parser. Differences in detail:

- **Apostrophes stay inside the word:** `McDonald's` is one token, `mcdonald's`, so a search for `mcdonalds` or `mcdonald` doesn't match it. The default parser splits it into `mcdonald|s` instead. Strip apostrophes before indexing if that matters.
- **Hyphens and ampersands split:** `7-Eleven` → `7|eleven`, `co-op` → `co|op`, `AT&T` → `at|t`.
- **URLs and hostnames stay whole:** `www.example.com` is one token.
- **No stemming or compound splitting.** `Kaffeehaus` doesn't match `haus`. For stemming, map `word` to a Snowball dictionary: `ALTER TEXT SEARCH CONFIGURATION my_config ALTER MAPPING FOR word WITH english_stem`.
