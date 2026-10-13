# wctl_newwidth: the width a new window takes from an explicit rect or width flag, 0 = none
newwidth 0 0 1000 600 new -r 0 0 161 117
newwidth 0 0 1000 600 new -dx 400
newwidth 0 0 1000 600 new -minx 100 -maxx 700
newwidth 0 0 1000 600 new -miny 130
newwidth 0 0 1000 600 new
newwidth 0 0 1000 600 new -dx 5000000
newwidth 0 0 1000 600 new -dx 99999
newwidth 0 0 1000 600 new -r 0 0 161 117 -pid 5 -cd /tmp
newwidth 0 0 1000 600 new -dx +100
newwidth 0 0 1000 600 new -dx -100
newwidth 0 0 1000 600 new -r 0 0 10 10
newwidth 200 100 1200 700 new -dx 300
newwidth 0 0 1000 600 new -hide -scroll
