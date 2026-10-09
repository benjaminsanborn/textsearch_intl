MODULE_big = textsearch_intl
OBJS = textsearch_intl.o
EXTENSION = textsearch_intl
DATA = textsearch_intl--1.0.sql
REGRESS = textsearch_intl scripts edge_cases queries headline normalize ngram config index

ICU_CFLAGS ?= $(shell pkg-config --cflags icu-uc)
ICU_LIBS ?= $(shell pkg-config --libs icu-uc)

PG_CPPFLAGS = $(ICU_CFLAGS)
SHLIB_LINK = $(ICU_LIBS)

PG_CONFIG ?= pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
