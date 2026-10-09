SET client_min_messages = warning;
CREATE EXTENSION IF NOT EXISTS icu_parser CASCADE;

CREATE FUNCTION tokens(doc text) RETURNS TABLE (alias text, token text)
LANGUAGE sql AS $$
  SELECT t.alias, p.token
  FROM ts_parse('icu', doc) p
  JOIN ts_token_type('icu') t ON t.tokid = p.tokid
  WHERE t.alias <> 'blank'
$$;

-- Chinese, simplified and traditional
SELECT * FROM tokens('北京大学图书馆');
SELECT * FROM tokens('國立臺灣大學');

-- Japanese: kanji, hiragana, katakana, mixed
SELECT * FROM tokens('東京駅前ラーメン店');
SELECT * FROM tokens('すし屋で寿司を食べました');
SELECT * FROM tokens('セブンイレブン');

-- Korean is space-separated
SELECT * FROM tokens('서울특별시 강남구 테헤란로');

-- Southeast Asian scripts without spaces
SELECT * FROM tokens('ร้านกาแฟสด');
SELECT * FROM tokens('ຂ້ອຍຮັກເຈົ້າຫຼາຍ');
SELECT * FROM tokens('ខ្ញុំស្រលាញ់អ្នកណាស់');
SELECT * FROM tokens('ကျွန်တော်မြန်မာစာသင်နေတယ်');

-- Right-to-left and other alphabets
SELECT * FROM tokens('שלום עולם צה״ל');
SELECT * FROM tokens('مطعم الشام');
SELECT * FROM tokens('नई दिल्ली');
SELECT * FROM tokens('Москва Αθήνα Hà Nội');

-- Mixed scripts, full-width forms, combining marks, emoji
SELECT * FROM tokens('iPhone手机壳');
SELECT * FROM tokens('ＡＢＣ１２３');
SELECT * FROM tokens(U&'Cafe\0301 cafe');
SELECT * FROM tokens(U&'pizza \D83C\DF55 \D83D\DC68\200D\D83D\DC69\200D\D83D\DC67 bar');

-- Numbers, punctuation inside words
SELECT * FROM tokens('1,000.50 3.14 -5 1e10 v1.2.3 24/7');
SELECT * FROM tokens('McDonald''s rock''n''roll 7-Eleven U.S.A. AT&T');
SELECT * FROM tokens('info@example.com https://example.com/a?b=c');

DROP FUNCTION tokens(text);
