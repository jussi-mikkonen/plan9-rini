# the full rio wctl grammar and its errors
wctl resize -dx 300
wctl resize -r 0 0 100 100 -dy 50
wctl move -minx +10 -miny -5 -maxx 40
wctl new -dx 400 -cd /tmp -pid 42 /rc/bin/window -x stats
wctl new -hide -scroll
wctl set -pid 7
wctl current
wctl top
wctl bottom
wctl hide
wctl unhide
wctl delete
wctl scroll
wctl noscroll
wctl set -id 3 -noscroll
wctl frob
wctl resize -bogus 3
wctl resize -dx
wctl resize -dx abc
wctl resize -dx 2000000
wctl resize extra
wctl resize -r 1 2 3
