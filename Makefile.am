SUBDIRS = lua tools scripts icons src . testcards
DIST_SUBDIRS = $(SUBDIRS)
ACLOCAL_AMFLAGS = -I build-aux/m4
EXTRA_DIST = \
    autogen.sh \
    version.txt \
    build-aux/gen-dynamic-list \
    build-aux/git-version-gen \
    doc

dist-hook:
	chmod u+w $(distdir)/version.txt; \
	echo "# this is filled in by \`make dist'; do not edit." \
	    >$(distdir)/version.txt; \
	echo "version=\"`sh build-aux/git-version-gen`\"" \
	    >>$(distdir)/version.txt
