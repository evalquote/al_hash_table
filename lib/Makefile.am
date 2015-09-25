SRCD	= $(top_builddir)/lib
INCD	= $(top_builddir)/include/alhash

pkginclude_HEADERS = $(INCD)/alhash.h
lib_LTLIBRARIES = libalhash.la

AM_CPPFLAGS = -I$(INCD) -I$(SRCD)

LIBSRCS = \
  hash.c

LIBHEADERS = \
  $(INCD)/alhash.h

libalhash_la_SOURCES = $(LIBSRCS) $(LIBHEADERS)
libalhash_la_LDFLAGS = -version-info $(LTVERSION)
libalhash_la_CFLAGS  = -I$(INCD)
