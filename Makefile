MODULE_big = icu_parser
OBJS = icu_parser.o
EXTENSION = icu_parser
DATA = icu_parser--1.0.sql
REGRESS = icu_parser

ICU_CFLAGS ?= $(shell pkg-config --cflags icu-uc)
ICU_LIBS ?= $(shell pkg-config --libs icu-uc)

PG_CPPFLAGS = $(ICU_CFLAGS)
SHLIB_LINK = $(ICU_LIBS)

PG_CONFIG ?= pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
