#!/bin/bash
ls imx-test-11.09.01_201112/*.patch | while read i; do
	echo "--- applying $i"
	patch -p1 --no-backup-if-mismatch < "$i"
	echo
done
