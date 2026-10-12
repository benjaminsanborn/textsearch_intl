SET client_min_messages = warning;
CREATE EXTENSION IF NOT EXISTS textsearch_intl CASCADE;

-- intl_normalize passes tokens it changes on to the next dictionary, and returns NULL for the rest
SELECT input, ts_lexize('intl_normalize', input)
FROM (VALUES ('plain'), ('Café'), ('McDonald''s'), ('McDonald’s'), ('ｾﾌﾞﾝ'), ('ＡＢＣ１２３'), ('㈱'),
             (U&'\0E19\0E4D\0E32'), (U&'\0E99\0ECD\0EB2'), ('น้ำ'), ('’'),
             ('ΑΘΗΝΑΣ'), ('Αθήνας'), ('Straße'), ('İstanbul'), ('Oʻahu'), ('Пʼятниця'), (U&'soft\00ADhyphen'),
             ('أحمد'), ('مدرسة'), ('کتابی'), ('مُحَمَّد'), ('مـطـعـم'), ('٧'), ('۷'), ('๗'), ('१२३')) v(input);

-- An elided article or an Arabic article gives two final lexemes at the token's position: the whole word and the word without it
SELECT input, ts_lexize('intl_normalize', input)
FROM (VALUES ('L''Occitane'), ('l’École'), ('dell''Arte'), ('qu''il'), ('O''Reilly'), ('l''a'),
             ('المطعم'), ('والمطعم'), ('الأهرام'), ('الف')) v(input);

-- intl_search chains intl_normalize, unaccent and simple (ts_debug shows only the first dictionary's output)
SELECT to_tsvector('intl_search', 'McDonald’s Café ｾﾌﾞﾝｲﾚﾌﾞﾝ Пятёрочка ร้านอาหาร ＡＢＣ１２３');

-- Names match regardless of accents, apostrophes, ё/е and character width
SELECT q, name,
       to_tsvector('intl_simple', name) @@ phraseto_tsquery('intl_simple', q) AS intl_simple,
       to_tsvector('intl_search', name) @@ phraseto_tsquery('intl_search', q) AS intl_search
FROM (VALUES ('mcdonalds', 'McDonald''s'),
             ('mcdonalds', 'McDonald’s Restaurant'),
             ('cafe', 'Café Müller'),
             ('café', 'Cafe Central'),
             ('пятёрочка', 'Пятерочка'),
             ('ラーメン', 'ﾗｰﾒﾝ屋'),
             ('abc', 'ＡＢＣストア'),
             ('ร้านอาหาร', 'ร้านอาหารครัวไทย'),
             ('อาหาร', 'ร้านอาหาร'),
             ('अस्पताल', 'सिटी अस्पताल'),
             ('セブンイレブン', 'セブン-イレブン渋谷店'),
             ('中国银行', '中国银行北京分行'),
             ('ΑΘΗΝΑΣ', 'Αθήνας'),
             ('athens strasse', 'Athens Straße'),
             ('istanbul', 'İstanbul'),
             ('oahu', 'Oʻahu Grill'),
             ('пятниця', 'Пʼятниця'),
             ('occitane', 'L''Occitane en Provence'),
             ('l''occitane', 'L’Occitane'),
             ('ecole', 'L’École des Beaux-Arts'),
             ('مطعم', 'المطعم الكبير'),
             ('المطعم', 'المطعم'),
             ('احمد', 'مطعم أحمد'),
             ('محمد', 'مُحَمَّد'),
             ('7', '٧ ستارز'),
             ('l''occitane', 'LOccitane'),
             ('المطعم', 'مطعم')) v(q, name);

-- Headlines keep the original text
SELECT ts_headline('intl_search', 'McDonald’s Café, Zürich', phraseto_tsquery('intl_search', 'mcdonalds cafe'));

-- The dictionary takes no options
CREATE TEXT SEARCH DICTIONARY intl_normalize_bad (TEMPLATE = intl_normalize_template, form = 'nfc');

-- Hiragana is written as katakana and traditional Chinese characters as simplified
SELECT input, ts_lexize('intl_normalize', input)
FROM (VALUES ('すし'), ('らーめん'), ('ゝ'), ('麥當勞'), ('國際機場'), ('麦当劳'), ('東京駅')) v(input);

-- Digits and letters are split apart, and a lowercase-to-uppercase change splits a word into parts kept at its position
SELECT to_tsvector('intl_search', '7-Eleven 7Eleven Sài Gòn CarrefourSA Express, Shop');

-- With max_length set, two words separated only by spaces or dashes, one of them at most that long, are also indexed run together
ALTER TEXT SEARCH DICTIONARY intl_join (max_length = 3);
SELECT to_tsvector('intl_search', '7-Eleven 7Eleven Sài Gòn CarrefourSA Express, Shop');
ALTER TEXT SEARCH DICTIONARY intl_join (max_length = 100);
SELECT phraseto_tsquery('intl_search', 'Seven Eleven'), phraseto_tsquery('intl_search', 'CarrefourSA');

SELECT q, name,
       to_tsvector('intl_search', name) @@ phraseto_tsquery('intl_search', q) AS intl_search,
       to_tsvector('intl_search_ngram', name) @@ phraseto_tsquery('intl_search_ngram', q) AS intl_search_ngram,
       to_tsvector('intl_search', name) @@ plainto_tsquery('intl_search', q) AS intl_search_and
FROM (VALUES ('7-Eleven', '7Eleven Bangkok'),
             ('7 eleven', '7-ELEVEN'),
             ('7eleven', '7 Eleven'),
             ('seveneleven', 'Seven Eleven'),
             ('seveneleven', 'Seven-Eleven'),
             ('Seven Eleven', 'SevenEleven'),
             ('Seven Eleven', 'Seveneleven'),
             ('saigon', 'Sài Gòn Coffee'),
             ('sai gon', 'Saigon Coffee'),
             ('mcdonalds', 'Mc Donald''s'),
             ('McDonald''s', 'MCDONALDS'),
             ('donalds', 'McDonald''s'),
             ('carrefour', 'CarrefourSA'),
             ('carrefoursa', 'Carrefour SA'),
             ('CarrefourSA', 'Carrefoursa'),
             ('carrefour express', 'CarrefourSA Express'),
             ('Carrefour SA', 'CarrefourSA'),
             ('cafe shop', 'Cafe, Shop'),
             ('cafeshop', 'Cafe, Shop'),
             ('макка', 'Мак-Ка'),
             ('route66', 'Route 66 Diner'),
             ('らーめん', 'ラーメン屋'),
             ('ラーメン', 'らーめん一番'),
             ('麥當勞', '麦当劳'),
             ('麦当劳', '麥當勞台北店'),
             ('机场', '國際機場')) v(q, name);

ALTER TEXT SEARCH DICTIONARY intl_join (max_length);
SELECT to_tsvector('intl_search', 'Mc Donald''s') @@ phraseto_tsquery('intl_search', 'mcdonalds') AS joined_by_default;

-- Unknown parameters are rejected
ALTER TEXT SEARCH DICTIONARY intl_join (min_length = 3);
