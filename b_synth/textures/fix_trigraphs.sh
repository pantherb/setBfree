#!/bin/sh

for file in uim*.c; do
	sed -i 's/??/\\77?/g' $file
done
