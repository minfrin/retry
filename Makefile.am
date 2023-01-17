
ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS = retry
retry_SOURCES = retry.c

EXTRA_DIST = retry.spec
dist_man_MANS = retry.1

retry.1: retry
	./retry --help | txt2man -s 1 -t "${PACKAGE_NAME}" -r "${PACKAGE_NAME}-${PACKAGE_VERSION}" > retry.1

