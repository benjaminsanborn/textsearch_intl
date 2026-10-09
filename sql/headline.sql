SET client_min_messages = warning;
CREATE EXTENSION IF NOT EXISTS textsearch_intl CASCADE;

SELECT ts_headline('intl_simple', 'The best ラーメン in Tokyo', plainto_tsquery('intl_simple', 'ラーメン'));
SELECT ts_headline('intl_simple', '東京駅前ラーメン店', plainto_tsquery('intl_simple', 'ラーメン'));
SELECT ts_headline('intl_simple', 'ร้านกาแฟสด ใกล้สยาม', plainto_tsquery('intl_simple', 'กาแฟ'));
SELECT ts_headline('intl_simple', '北京大学图书馆', plainto_tsquery('intl_simple', '图书馆'),
                   'StartSel=[, StopSel=]');
SELECT ts_headline('intl_simple',
                   'one two three four five six seven eight nine ten 東京 eleven twelve thirteen',
                   plainto_tsquery('intl_simple', '東京'),
                   'MaxWords=5, MinWords=2');
SELECT ts_headline('intl_simple',
                   'alpha 東京 beta gamma delta epsilon zeta eta theta 東京 iota',
                   plainto_tsquery('intl_simple', '東京'),
                   'MaxFragments=2, MaxWords=3, MinWords=1, FragmentDelimiter=" | "');
