MODULE_big = icu_parser
OBJS = icu_parser.o
EXTENSION = icu_parser
DATA = icu_parser--1.1.sql
REGRESS = icu_parser scripts edge_cases queries headline normalize config index

ICU_CFLAGS ?= $(shell pkg-config --cflags icu-uc)
ICU_LIBS ?= $(shell pkg-config --libs icu-uc)

PG_CPPFLAGS = $(ICU_CFLAGS)
SHLIB_LINK = $(ICU_LIBS)

PG_CONFIG ?= pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
