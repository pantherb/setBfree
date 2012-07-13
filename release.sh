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

echo "Update version number -- edit Makefile debian/changelog"
echo -n "launch editor ? [Y/n]"
read -n1 a
echo

if test "$a" != "n" -a "$a" != "N"; then
	${EDITOR} Makefile debian/changelog
fi

eval `grep "VERSION=" Makefile`
echo "   VERSION: v${VERSION}"

echo -n "Is this correct? [Y/n]"
read -n1 a
echo
if test "$a" == "n" -o "$a" == "N"; then
	exit 1
fi

echo "re-creating man-pages and documentation with new version-number.."
# re-create man-pages
make clean
make ENABLE_CONVOLUTION=yes PREFIX=/usr
make doc
make dist

echo "creating git-commit of updated doc & version number"
git commit -m "finalize changelog" Makefile debian/changelog doc/*.1

git tag "v${VERSION}" || (echo -n "version tagging failed. - press Enter to continue, CTRL-C to stop."; read; ) 

echo -n "git push? [Y/n]"
read -n1 a
echo

if test "$a" != "n" -a "$a" != "N"; then
	git push origin
	#git push --tags ## would push ALL existing tags,
	git push origin "refs/tags/v${VERSION}:refs/tags/v${VERSION}"
fi

ls -l "setbfree-${VERSION}.tar.gz"

# TODO - upload programatically
# http://developer.github.com/v3/repos/downloads/
# https://github.com/typester/net-github-upload-perl
# https://github.com/Constellation/ruby-net-github-upload

GITREPO="pantherb/setBfree"
x-www-browser https://github.com/${GITREPO}/downloads
