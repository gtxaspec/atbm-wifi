#!/bin/sh
HMAC_SVN_REV=$(((svnversion | grep -qv exported && echo -n 'Revision: ' && svnversion) || git svn info | sed -e 's/$$$$/M/' | grep '^Revision: ' ) | sed -e 's/^Revision: //'| sed -e 's/M//g')
echo "#define SVN_VERSION" $HMAC_SVN_REV>include/svn_version.h
echo "the revision is" $HMAC_SVN_REV
