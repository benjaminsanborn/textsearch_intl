SET client_min_messages = warning;
CREATE EXTENSION IF NOT EXISTS icu_parser CASCADE;

-- icu_normalize passes tokens it changes on to the next dictionary, and returns NULL for the rest
SELECT input, ts_lexize('icu_normalize', input)
FROM (VALUES ('plain'), ('Café'), ('McDonald''s'), ('McDonald’s'), ('ｾﾌﾞﾝ'), ('ＡＢＣ１２３'), ('㈱'),
             (U&'\0E19\0E4D\0E32'), (U&'\0E99\0ECD\0EB2'), ('น้ำ'), ('’')) v(input);

-- icu_search chains icu_normalize, unaccent and simple (ts_debug shows only the first dictionary's output)
SELECT to_tsvector('icu_search', 'McDonald’s Café ｾﾌﾞﾝｲﾚﾌﾞﾝ Пятёрочка ร้านอาหาร ＡＢＣ１２３');

-- Names match regardless of accents, apostrophes, ё/е and character width
SELECT q, name,
       to_tsvector('icu_simple', name) @@ phraseto_tsquery('icu_simple', q) AS icu_simple,
       to_tsvector('icu_search', name) @@ phraseto_tsquery('icu_search', q) AS icu_search
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
             ('中国银行', '中国银行北京分行')) v(q, name);

-- Headlines keep the original text
SELECT ts_headline('icu_search', 'McDonald’s Café, Zürich', phraseto_tsquery('icu_search', 'mcdonalds cafe'));

-- The dictionary takes no options
CREATE TEXT SEARCH DICTIONARY icu_normalize_bad (TEMPLATE = icu_normalize_template, form = 'nfc');
