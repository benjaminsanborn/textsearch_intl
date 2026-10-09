SET client_min_messages = warning;
CREATE EXTENSION IF NOT EXISTS textsearch_intl CASCADE;

-- intl_ngram expands Han, kana and Hangul tokens into characters and adjacent pairs; anything else passes through
SELECT input, ts_lexize('intl_ngram', input)
FROM (VALUES ('편의점'), ('駅'), ('東京駅'), ('ラーメン'), ('abc'), ('東京12'), ('กาแฟ')) v(input);

SELECT to_tsvector('intl_search_ngram', '행복한편의점 McDonald’s');
SELECT phraseto_tsquery('intl_search_ngram', '스타벅스 안양');

-- Words inside Korean and CJK tokens match; ICU word boundaries between tokens still hold
SELECT q, name,
       to_tsvector('intl_search', name) @@ phraseto_tsquery('intl_search', q) AS intl_search,
       to_tsvector('intl_search_ngram', name) @@ phraseto_tsquery('intl_search_ngram', q) AS intl_search_ngram
FROM (VALUES ('편의점', '행복한편의점'),
             ('스타벅스', '스타벅스안양평촌점'),
             ('서울', '서울의 맛집'),
             ('병원', '서울대학교병원'),
             ('公園', '中村公園店'),
             ('駅', '駅前'),
             ('스타벅스 안양', '스타벅스 안양평촌점'),
             ('안양 스타벅스', '스타벅스 안양평촌점'),
             ('京都', '東京都'),
             ('스타벅스', '파스타벅스'),
             ('主教', '天主教'),
             ('กาแฟ', 'ร้านกาแฟสด'),
             ('mcdonalds', 'McDonald''s')) v(q, name);

-- The dictionary takes no options
CREATE TEXT SEARCH DICTIONARY intl_ngram_bad (TEMPLATE = intl_ngram_template, size = 3);
