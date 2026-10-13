#!/bin/bash
set -eu
cd "$(dirname "$0")"
pass=0
fail=0
for t in layouttest:script:golden kbdtest:kscript:kgolden wintest:wscript:wgolden termtest:tscript:tgolden ctltest:cscript:cgolden; do
	IFS=: read -r drv sx gx <<< "$t"
	for s in tests/*.$sx; do
		[ -f "$s" ] || continue
		n=$(basename "$s" .$sx)
		g="tests/$n.$gx"
		if [ ! -f "$g" ]; then
			echo "FAIL $n (no golden)"
			fail=$((fail+1))
			continue
		fi
		if ./$drv < "$s" 2>&1 | diff -u "$g" - ; then
			echo "PASS $n"
			pass=$((pass+1))
		else
			echo "FAIL $n"
			fail=$((fail+1))
		fi
	done
done
echo "total $((pass+fail)) pass $pass fail $fail"
[ "$fail" -eq 0 ]
