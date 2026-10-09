SET client_min_messages = warning;
CREATE EXTENSION IF NOT EXISTS textsearch_intl CASCADE;

CREATE TABLE names (id int, name text);
INSERT INTO names VALUES
  (1, '星巴克咖啡 南京西路店'),
  (2, 'สตาร์บัคส์ สยามพารากอน'),
  (3, 'ร้านสตาร์บัคส์สยาม'),
  (4, 'セブンイレブン新宿店'),
  (5, 'セブン-イレブン 渋谷店'),
  (6, '東京駅前ラーメン店'),
  (7, 'Blue Bottle Coffee'),
  (8, 'Café de Flore'),
  (9, '麦当劳(人民广场店)'),
  (10, '서울 강남 스타벅스');

CREATE FUNCTION hits(q tsquery) RETURNS int[]
LANGUAGE sql AS $$
  SELECT coalesce(array_agg(id ORDER BY id), '{}')
  FROM names WHERE to_tsvector('intl_simple', name) @@ q
$$;

-- Queries are segmented with the same parser as documents
SELECT plainto_tsquery('intl_simple', '星巴克');
SELECT plainto_tsquery('intl_simple', 'ラーメン店');
SELECT phraseto_tsquery('intl_simple', '東京駅前');

SELECT hits(plainto_tsquery('intl_simple', '星巴克'));
SELECT hits(plainto_tsquery('intl_simple', 'สตาร์บัคส์'));
SELECT hits(plainto_tsquery('intl_simple', 'セブンイレブン'));
SELECT hits(plainto_tsquery('intl_simple', 'セブン'));
SELECT hits(plainto_tsquery('intl_simple', 'ラーメン'));
SELECT hits(plainto_tsquery('intl_simple', '麦当劳'));
SELECT hits(plainto_tsquery('intl_simple', 'coffee'));
SELECT hits(plainto_tsquery('intl_simple', 'CAFÉ'));
SELECT hits(plainto_tsquery('intl_simple', '스타벅스'));

-- Phrase, prefix, negation and OR
SELECT hits(phraseto_tsquery('intl_simple', '東京駅前'));
SELECT hits(phraseto_tsquery('intl_simple', '駅前東京'));
SELECT hits(to_tsquery('intl_simple', 'bott:*'));
SELECT hits(to_tsquery('intl_simple', 'ラーメ:*'));

-- A CJK prefix can segment differently from the full word, so prefix matching is unreliable
SELECT to_tsquery('intl_simple', 'ラー:*');
SELECT hits(to_tsquery('intl_simple', 'ラー:*'));
SELECT hits(websearch_to_tsquery('intl_simple', 'スターバックス or 星巴克 or "Blue Bottle"'));
SELECT hits(websearch_to_tsquery('intl_simple', 'セブン -渋谷'));

-- The built-in parser keeps each unspaced run as one token, so none of these match
SELECT coalesce(array_agg(id ORDER BY id), '{}') AS default_parser_hits
FROM names
WHERE to_tsvector('simple', name) @@ plainto_tsquery('simple', 'ラーメン')
   OR to_tsvector('simple', name) @@ plainto_tsquery('simple', '星巴克');

-- Ranking works on ICU positions
SELECT id, round(ts_rank(to_tsvector('intl_simple', name), plainto_tsquery('intl_simple', 'セブン 店'))::numeric, 4) AS rank
FROM names WHERE id IN (4, 5) ORDER BY id;

DROP FUNCTION hits(tsquery);
DROP TABLE names;
