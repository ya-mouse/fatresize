sbin_PROGRAMS = fatresize

man_MANS = fatresize.1

fatresize_CPPFLAGS = $(PARTED_CFLAGS)

fatresize_SOURCES = fatresize.c
fatresize_LDADD = $(PARTED_LIBS)

EXTRA_DIST = AUTHORS CREDITS ChangeLog NEWS README \
	fatresize.spec $(man_MANS)

SUFFIXES = .1
CLEANFILES = $(man_MANS)

.sgml.1:
	docbook-to-man $< > $@
