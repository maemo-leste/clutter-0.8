#! /bin/sh

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

PROJECT=Clutter
TEST_TYPE=-d
FILE=clutter

test $TEST_TYPE $FILE || {
        echo "You must run this script in the top-level $PROJECT directory"
        exit 1
}

GTKDOCIZE=`which gtkdocize`
if test -z $GTKDOCIZE; then
        echo "*** No gtk-doc support ***"
        echo "EXTRA_DIST =" > gtk-doc.make
else
        gtkdocize || exit $?
fi

GLIB_GETTEXTIZE=`which glib-gettextize`
if test -z $GLIB_GETTEXTIZE; then
        echo "*** No glib-gettextize ***"
        exit 1
else
        glib-gettextize || exit $?
fi

autoreconf -v --install || exit $?

./configure "$@" && echo "Now type 'make' to compile $PROJECT."
