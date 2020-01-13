
bin_PROGRAMS = retry
retry_SOURCES = retry.c

EXTRA_DIST = retry.spec debian/changelog debian/compat debian/control debian/copyright debian/docs debian/dirs debian/rules debian/source/format
dist_man_MANS = retry.1

retry.1: retry.c $(top_srcdir)/configure.ac
	which help2man && help2man ./retry > retry.1 || true

