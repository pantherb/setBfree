#!/bin/sh

DEBRELEASE=$(head -n1 debian/changelog | cut -d ' ' -f 2 | sed 's/[()]*//g')

TMPDIR=/tmp/setbfree-${DEBRELEASE}
rm -rf ${TMPDIR}

echo "debian -export-ignore" >> .git/info/attributes

git-buildpackage \
	--git-upstream-branch=master --git-debian-branch=master \
	--git-upstream-tree=branch \
	--git-export-dir=${TMPDIR} --git-cleaner=/bin/true \
	--git-force-create \
	-rfakeroot $@ 

ERROR=$?

ed -s .git/info/attributes > /dev/null << EOF
/debian -export-ignore
d
wq
EOF

if test $ERROR != 0; then
	exit $ERROR
fi

lintian -i --pedantic ${TMPDIR}/setbfree_${DEBRELEASE}_*.changes \
	| tee /tmp/jrec.issues

echo
ls ${TMPDIR}/setbfree_${DEBRELEASE}_*.changes
echo
echo dput rg42 ${TMPDIR}/setbfree_${DEBRELEASE}_*.changes
