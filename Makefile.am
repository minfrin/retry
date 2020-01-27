
ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS = retry
retry_SOURCES = retry.c

EXTRA_DIST = retry.spec debian/changelog debian/compat debian/control debian/copyright debian/docs debian/dirs debian/rules debian/source/format
dist_man_MANS = retry.1

retry.1: retry.c $(top_srcdir)/configure.ac
	which txt2man && ./retry --help | txt2man -d 1 -t "${PACKAGE_NAME}" -r "${PACKAGE_NAME}-${PACKAGE_VERSION}" > retry.1 || true

