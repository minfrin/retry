
bin_PROGRAMS = repeat
repeat_SOURCES = repeat.c

EXTRA_DIST = repeat.spec debian/changelog debian/compat debian/control debian/copyright debian/docs debian/dirs debian/rules debian/source/format
dist_man_MANS = repeat.1

repeat.1: repeat
	which help2man && help2man ./repeat > repeat.1 || true

