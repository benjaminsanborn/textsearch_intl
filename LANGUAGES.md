# How textsearch_intl performs by language

textsearch_intl splits text with ICU's word-break rules. For scripts written without spaces (Chinese, Japanese, Thai, Lao, Khmer, Burmese), ICU looks words up in its dictionaries; everywhere else it splits on spaces and punctuation like the default parser does. So it helps most where the default parser fails completely, and it does nothing for problems that come from word *forms*: particles, clitics, compounds and inflection.

The evidence comes from three places:

- **Benchmark:** 500,000 Wikipedia titles per language, with recall and overmatch scored on 250 labeled term/title pairs per language ([bench/results.md](bench/results.md)). Recall is the share of real word matches a method returns; overmatch is the share of its results that aren't real matches.
- **Production place names:** single-word searches against a table of about 550 million business and place names, comparing textsearch_intl with the default parser (`simple`) and a pg_trgm substring index.
- **Examples:** every tokenization below was produced by textsearch_intl on ICU 78.3. Another ICU version can split some dictionary words differently.

## Summary

| Language | Verdict | Benchmark recall (default → intl_search → intl_search_ngram) | Overmatch (intl_search / ngram) | Main weakness |
|---|---|--:|--:|---|
| Chinese | **Good** | 10% → 93% → 99% | 16% / 18% | Transliterated names split into single characters; some compounds stay whole |
| Japanese | **Good for kanji and katakana** | 7% → 86% → 100% | 20% / 54% | Hiragana words split into syllables with intl_search; compounds |
| Thai | **Good with intl_search_ngram** | 5% → 79% → 98% | 44% / 69% | intl_search alone splits unknown names and loanwords unpredictably |
| Lao, Khmer, Burmese | Likely good (not benchmarked) | | | Same dictionary approach as Thai |
| Korean | **Good with intl_search_ngram** | 47% → 48% → 100% | 1% / 22% | intl_search alone leaves particles and compound names attached |
| Vietnamese | No gain | | | Words span several space-separated syllables |
| Arabic | Better with `intl_search` | | | Prepositions other than `و` stay attached |
| Hebrew | No gain | | | Articles and prepositions are attached to words |
| English and other spaced languages | Same as default | | | No stemming or compound splitting |

## Advice that applies to every language

- **Query with `phraseto_tsquery`, not `plainto_tsquery`.** When ICU splits a name into pieces, a plain query requires the pieces to appear anywhere in the title, in any order. `星巴克` (Starbucks) splits into `星|巴|克`, so `plainto_tsquery` matches `克巴星餐厅`; `phraseto_tsquery` requires them adjacent and in order, and doesn't.
- **Use the `intl_search` configuration.** It applies NFKC, case folding, accent folding and apostrophe removal to each token, so half-width katakana (`ｾﾌﾞﾝ`), full-width Latin (`ＡＢＣ`), case (`ΑΘΗΝΑΣ`, `STRASSE`), accents (`café`, `ё`), apostrophes (`McDonald's`, `Oʻahu`), Arabic letter variants (`أحمد`) and non-ASCII digits (`٧`) match the way users type them. It also indexes French and Italian words with and without an elided article (`L'Occitane` and `occitane`), and Arabic words with and without the article. It keeps the Thai and Lao vowels intact, which a plain `normalize(name, NFKC)` before tokenizing would split (`จำกัด` → `จํา|กัด`).
- **Use `intl_search_ngram` for Korean, Chinese and Japanese.** It adds character unigrams and bigrams for Hangul, Han and kana tokens, so a word matches inside a longer word: particles, compounds and branch names. It costs overmatch, the same as pg_bigm's, and leaves every other script as `intl_search` does.
- **Pair it with an n-gram index for names ICU doesn't know.** Outside Hangul, Han and kana, textsearch_intl matches whole words only. For Thai fragments and partial Latin words, add pg_bigm (or pg_trgm for terms of three or more characters) and OR the two conditions.

## Chinese: good

The biggest improvement of any language. On Wikipedia titles, recall goes from 10% with the default parser to 93%, at 16% overmatch against 18% for substring search. On place names, single-word searches for common words found 100–1,200 times more rows than the default parser:

| Search | default parser | textsearch_intl | pg_trgm |
|---|--:|--:|--:|
| 公園 (park) | 315 | 116,892 | timed out |
| 銀行 (bank) | 45 | 54,071 | timed out |
| 機場 (airport) | 44 | 5,027 | timed out |
| 麥當勞 (McDonald's) | 680 | 722 | 722 |
| 星巴克 (Starbucks) | 922 | 1,169 | 1,173 |

pg_trgm can't use its index for one- and two-character terms, so those searches read the whole index and timed out at 20 seconds.

Where it falls short:

- **Transliterated foreign names** aren't in ICU's dictionary and come out as single characters: `朱莉娅·加布里埃莱斯基` → `朱|莉|娅|加布里|埃|莱|斯|基`, and `星巴克咖啡` → `星|巴|克|咖啡`. A phrase query still finds them, but a plain query overmatches.
- **Some compounds stay whole.** `國際機場` (international airport, Traditional) is one token, so a search for `機場` misses it, while the Simplified `国际机场` splits into `国际|机场`. This is most of the 7% the benchmark missed, and `intl_search_ngram` finds it (99% recall, 18% overmatch).
- **Traditional and Simplified characters** match each other with `intl_search` and `intl_search_ngram`, which write traditional characters as simplified: `麥當勞` finds `麦当劳` and the other way round. `intl_simple` keeps them apart.

## Japanese: good for kanji and katakana, weak for hiragana

Recall goes from 7% to 86%, with 20% overmatch against 54% for substring search. Kanji compounds split well (`東京国際空港` → `東京|国際|空港`, so a search for `空港` finds it). Katakana splits at hyphens and middle dots: `セブン-イレブン渋谷店` → `セブン|イレブン|渋谷|店`, so a search for `セブンイレブン` finds the hyphenated form. On place names that found 18,268 rows, against 541 for a substring search that expected no hyphen.

Where it falls short:

- **Hiragana-only words split into syllables:** `ひらがな` → `ひ|ら|が|な`, `やきとり` → `や|き|とり`, `らーめん` → `ら|ー|めん`. Verb forms fragment the same way: `食べました` → `食|べ|ま|した`. `intl_normalize` writes hiragana as katakana, and `intl_search_ngram`'s parser keeps adjacent kana words together as one run, so `らーめん` and `ラーメン` find each other there. With `intl_search` they still don't, because the hiragana spelling is split and the katakana one isn't.
- **Unknown katakana names** can come out as fragments (`ンズ`, `ジョ`), which then match inside unrelated names. This is most of the 20% overmatch.
- **Half-width katakana** (`ｾﾌﾞﾝｲﾚﾌﾞﾝ`) only matches full-width with `intl_search`, which applies NFKC.
- **No morphology.** ICU isn't a morphological analyzer like MeCab or Kuromoji, so it won't link inflected forms or break up every compound.

`intl_search_ngram` raises recall to 100% and finds words inside compounds (`駅` in `駅前`), but overmatch rises to 54%, the same as pg_bigm's, because Japanese titles are full of short kana sequences that also occur inside other words.

## Thai: use intl_search_ngram

Recall goes from 5% to 79%, at 44% overmatch against 77% for substring search. Dictionary words split cleanly: `ร้านกาแฟสด` → `ร้าน|กาแฟ|สด`, `โรงพยาบาลกรุงเทพ` → `โรง|พยาบาล|กรุงเทพ`. On place names, textsearch_intl found 39,019 rows for `กาแฟ` (coffee) against 3,808 for the default parser and 40,492 for substring search.

Where it falls short:

- **Unknown names pull letters off the next word.** `กุ๊งกิ๊งกาแฟโบราณ` → `กุ๊|งกิ๊|งกาแฟ|โบราณ`: the name isn't in the dictionary, so `กาแฟ` picks up its last letter and a search for it misses.
- **Loanwords split differently depending on what follows them.** `เซเว่น` (Seven) on its own is `เซ|เว่น`, but in `เซเว่นอีเลฟเว่น` (7-Eleven) it's `เซ|เว่|นอีเลฟเว่น`, so a search for `เซเว่น` doesn't find the full brand name. On place names, textsearch_intl found 2,094 rows against 3,794 for substring search.
- **Thai followed directly by Latin letters stays joined:** `ร้านกาแฟcoffee` → `ร้าน|กาแฟcoffee`.
- **Fragments overmatch.** Short fragments like `อง` and `ซ์` come out of unknown names as tokens and match inside unrelated words.

`intl_search_ngram` avoids ICU's guesses: its `intl_runs` parser keeps each unspaced Thai run whole and indexes every pair and every three adjacent grapheme clusters, so `เซเว่น` finds `เซเว่นอีเลฟเว่น` and `กาแฟ` finds `กุ๊งกิ๊งกาแฟโบราณ`. Recall goes to 98% at 69% overmatch, against 77% for substring search; the overmatch is high on this benchmark because its query terms include ICU fragments like `อง`. The index is about three times `intl_search`'s for Thai text.

## Lao, Khmer, Burmese: likely good, not benchmarked

ICU has dictionaries for all three, and sentences split into words: `ຮ້ານອາຫານລາວ` → `ຮ້ານ|ອາຫານ|ລາວ`, `ភោជនីយដ្ឋានខ្មែរ` → `ភោជនីយដ្ឋាន|ខ្មែរ`, `မြန်မာစားသောက်ဆိုင်` → `မြန်မာ|စားသောက်ဆိုင်`. Expect the same weaknesses as Thai with unknown names.

## Korean: use intl_search_ngram

Korean puts spaces between phrases, but attaches particles, endings and other nouns directly to a word. ICU has no Korean dictionary, so each space-separated run is one token, exactly as with the default parser. Recall was 48% against 47% for the default parser.

- **Particles:** `서울의 맛집` → `서울의|맛집`, so a search for `서울` doesn't match.
- **Brand plus branch names:** `스타벅스안양평촌점` (Starbucks, Anyang Pyeongchon branch) is a single token. On place names, a search for `편의점` (convenience store) found 2,041 rows with textsearch_intl and 9,096 with substring search, because names like `행복한편의점` are one word.

`intl_search_ngram` fixes both: it indexes every Hangul syllable and syllable pair, so `서울` matches `서울의`, `편의점` matches `행복한편의점`, and `스타벅스` matches `스타벅스안양평촌점`. Recall goes to 100% with 22% overmatch, the same as pg_bigm, at a similar index size. The overmatch is the substring kind: `스타벅스` also matches `파스타벅스`. For true morphology, use an analyzer outside Postgres (such as Elasticsearch's nori).

## Vietnamese: no gain

Vietnamese writes each syllable separately, and most words are two or more syllables. ICU splits at every space, like the default parser: `Nhà hàng Phở Hà Nội` → `Nhà|hàng|Phở|Hà|Nội`. Use `phraseto_tsquery` so `Hà Nội` matches as a unit. `intl_search` folds the diacritics. With `intl_join`'s `max_length` set it also indexes adjacent short syllables run together, so `hanoi` finds `Hà Nội` and `saigon` finds `Sài Gòn`; the other direction (`sai gon` finding `Saigon`) still doesn't match.

## Arabic: better with intl_search; Hebrew: no gain

Spaces separate words, but the article and some prepositions and conjunctions are written as part of the word. `intl_search` indexes a word that starts with the article `ال` or `وال` both with and without it, so a search for `مطعم` (restaurant) finds `المطعم` (the restaurant). It also folds the letter variants people type interchangeably in names (`أحمد` and `احمد`, `مدرسة` and `مدرسه`, Persian `ی` and `ک`) and removes short-vowel marks. Other prefixes stay attached: `بالمطعم` (in the restaurant) is one token, and a word that merely starts with `ال` (`الله`) also gets the shortened form, a small source of overmatch.

Hebrew does the same with `המסעדה` against `מסעדה`, and textsearch_intl doesn't remove Hebrew prefixes.

## English and other spaced languages: same as the default parser

For English, Russian, Greek, Hindi, German, Turkish and other languages that separate words with spaces, ICU's splits match the default parser's, and tokenizing English is slightly faster (0.63 µs against 0.73 µs per title). Search results were the same within 0.5%: `starbucks` found 54,395 place names with textsearch_intl and 54,352 with the default parser. Differences in detail:

- **Apostrophes stay inside the word:** `McDonald's` is one token, `mcdonald's`, so a search for `mcdonalds` or `mcdonald` doesn't match it. The default parser splits it into `mcdonald|s` instead. `intl_search` removes the apostrophe, so `mcdonalds` matches, and indexes French and Italian elisions both ways, so `occitane` finds `L'Occitane`.
- **Case folds fully:** `intl_search` uses Unicode case folding rather than the database's `lower()`, so Greek final sigma (`ΑΘΗΝΑΣ`, `Αθήνας`), German `ß` (`STRASSE`) and Turkish `İ` match.
- **Hyphens and ampersands split:** `7-Eleven` → `7|eleven`, `co-op` → `co|op`, `AT&T` → `at|t`. Digits and letters split too (`7Eleven` → `7|eleven`).
- **Case changes inside a word split:** `intl_search` indexes `CarrefourSA` as the whole word and as its parts, so `carrefour` finds it.
- **Words written together or apart** (`Mc Donald's` and `McDonald's`) match only with `intl_join`'s `max_length` set, which adds the two words run together (see the README).
- **URLs and hostnames stay whole:** `www.example.com` is one token.
- **No stemming or compound splitting.** `Kaffeehaus` doesn't match `haus`. For stemming, map `word` to a Snowball dictionary: `ALTER TEXT SEARCH CONFIGURATION my_config ALTER MAPPING FOR word WITH english_stem`.
