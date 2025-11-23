#http://blog.pgxn.org/post/4783001135/extension-makefiles pg makefiles

EXTENSION = automerge
PG_CONFIG ?= pg_config
DATA = $(wildcard src/automerge/*--*.sql)
PGXS := $(shell $(PG_CONFIG) --pgxs)
MODULE_big = automerge
OBJS = $(patsubst %.c,%.o,$(shell find src/automerge -name '*.c'))

# Static linking configuration (recommended for production)
# To use static linking, set AUTOMERGE_STATIC=1 when building:
#   make AUTOMERGE_STATIC=1
# This embeds libautomerge into the extension, making it deployable on
# managed PostgreSQL platforms.
ifeq ($(AUTOMERGE_STATIC),1)
    AUTOMERGE_LIB_PATH ?= /usr/local/lib/libautomerge.a
    SHLIB_LINK = -lc -lpq $(AUTOMERGE_LIB_PATH) -lpthread -ldl -lm
else
    # Dynamic linking (default, requires libautomerge.so at runtime)
    SHLIB_LINK = -lc -lpq -lautomerge
endif

REGRESS := $(shell find sql -name '*.sql' -exec basename {} .sql \;)

include $(PGXS)
override CFLAGS += -std=c11 -g3 -O2

docgen:
	python3 docgen.py
