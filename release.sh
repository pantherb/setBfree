#!/bin/bash

EDITOR=${EDITOR:-editor}

if test ! -d .git; then
	echo "ERROR: This script runs only from a git repository."
	exit 1
fi

if test "`git symbolic-ref HEAD`" != "refs/heads/master"; then
	echo "ERROR: The git 'master' branch must be checked out."
	exit 1
fi

echo "commit pending changes.."
git commit -a 

echo "Update version number -- edit two files: Makefile ChangeLog"
echo -n "launch editor ? [Y/n]"
read -n1 a
echo

if test "$a" != "n" -a "$a" != "N"; then
	${EDITOR} Makefile ChangeLog
fi

eval `grep "EXPORTED_VERSION=" Makefile`
echo "   VERSION: v${EXPORTED_VERSION}"

echo -n "Is this correct? [Y/n]"
read -n1 a
echo
if test "$a" == "n" -o "$a" == "N"; then
	exit 1
fi

echo "re-creating man-pages and documentation with new version-number.."
# re-create man-pages
make clean
make PREFIX=/usr VERSION=${EXPORTED_VERSION}
make doc
make dist

echo "creating git-commit of updated doc & version number"
git commit -m "finalize changelog v${EXPORTED_VERSION}" Makefile ChangeLog doc/*.1

git tag "v${EXPORTED_VERSION}" || (echo -n "version tagging failed. - press Enter to continue, CTRL-C to stop."; read; ) 

echo -n "git push? [Y/n]"
read -n1 a
echo

if test "$a" != "n" -a "$a" != "N"; then
	for remote in $(git remote); do
		git push $remote || exit 1
		#git push --tags ## would push ALL existing tags,
		git push $remote "refs/tags/v${EXPORTED_VERSION}:refs/tags/v${EXPORTED_VERSION}" || exit 1
	done
fi

ls -l "setbfree-${EXPORTED_VERSION}.tar.gz"
