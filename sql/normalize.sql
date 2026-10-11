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
