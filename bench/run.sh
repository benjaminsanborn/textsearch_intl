#!/usr/bin/env bash
set -euo pipefail

BENCH_DIR="$(cd "$(dirname "$0")" && pwd)"
DB="${BENCH_DB:-textsearch_intl_bench}"
DUMP="${BENCH_DUMP:-20260901}"
SAMPLE="${BENCH_SAMPLE:-500000}"
RUNS="${BENCH_RUNS:-5}"
LANGS="${BENCH_LANGS:-en zh ja ko th}"
CTYPE="${BENCH_CTYPE:-$(locale -a 2>/dev/null | grep -ixE 'C\.UTF-?8' | head -n 1 || true)}"
CTYPE="${CTYPE:-en_US.UTF-8}"
DATA="$BENCH_DIR/data"
OUT="${BENCH_OUT:-$BENCH_DIR/results.md}"

set -- $LANGS
STEPS=$(( $# * 2 + 2 ))
STEP=0
progress() {
  STEP=$((STEP + 1))
  echo "@@progress {\"done\": $STEP, \"total\": $STEPS, \"msg\": \"$1\", \"ts\": $(date +%s)}"
}

mkdir -p "$DATA"
for lang in $LANGS; do
  progress "download $lang"
  file="$DATA/${lang}wiki-$DUMP-all-titles-in-ns0.gz"
  if [ ! -s "$file" ]; then
    curl -fsSL -o "$file.part" "https://dumps.wikimedia.org/${lang}wiki/$DUMP/${lang}wiki-$DUMP-all-titles-in-ns0.gz"
    mv "$file.part" "$file"
  fi
done

dropdb --if-exists "$DB"
createdb -T template0 -E UTF8 --locale-provider=libc --lc-collate=C --lc-ctype="$CTYPE" "$DB"
psql -X -q -v ON_ERROR_STOP=1 -d "$DB" <<SQL
CREATE EXTENSION textsearch_intl CASCADE;
CREATE EXTENSION pg_trgm;
CREATE UNLOGGED TABLE titles_raw (lang text, title text);
CREATE TABLE labels (lang text, term text, title text, label text CHECK (label IN ('match', 'over')));
SQL
if [ -s "$BENCH_DIR/labels.csv" ]; then
  psql -X -q -v ON_ERROR_STOP=1 -d "$DB" -c "\\copy labels FROM '$BENCH_DIR/labels.csv' WITH CSV HEADER"
fi
psql -X -q -d "$DB" -c "CREATE EXTENSION pg_bigm" 2>/dev/null || echo "pg_bigm is not installed; skipping it"

for lang in $LANGS; do
  progress "load $lang"
  gzip -dc "$DATA/${lang}wiki-$DUMP-all-titles-in-ns0.gz" \
    | tail -n +2 \
    | sed -e 's/\\/\\\\/g' -e 's/_/ /g' -e "s/^/$lang\t/" \
    | psql -X -q -v ON_ERROR_STOP=1 -d "$DB" -c "\copy titles_raw FROM STDIN"
done

progress "benchmark"
psql -X -q -v ON_ERROR_STOP=1 -d "$DB" \
  -v sample="$SAMPLE" -v runs="$RUNS" -v langs="$LANGS" \
  -f "$BENCH_DIR/bench.sql"

{
  echo "# Benchmark results"
  echo
  echo "| | |"
  echo "|---|---|"
  echo "| PostgreSQL | $(psql -X -At -d "$DB" -c "SELECT version()") |"
  echo "| ICU | $(psql -X -At -d "$DB" -c "SELECT intl_icu_version()") |"
  echo "| CPU | $( (sysctl -n machdep.cpu.brand_string 2>/dev/null || grep -m1 'model name' /proc/cpuinfo | cut -d: -f2) | sed 's/^ *//') |"
  echo "| Corpus | Wikipedia article titles, dump $DUMP, $SAMPLE titles per language |"
  echo "| Runs | median of $RUNS after one warm-up run, one backend, no parallel workers, JIT off |"
  echo "| LC_CTYPE | $CTYPE |"
  echo
  psql -X -q -At -d "$DB" -f "$BENCH_DIR/report.sql"
} > "$OUT"

progress "report $OUT"
