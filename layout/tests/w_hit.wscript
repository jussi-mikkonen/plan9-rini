# hit test: topmost wins, parked and empty places never hit
clear
place 1 4 4 500 596
place 2 200 100 400 300 f
hit 300 200
hit 100 200
hit 450 500
hit 600 300
# a parked entry at the same point is skipped
clear
place 1 4 4 500 596
place 3 200 100 400 300 f parked
hit 300 200
# an empty rect is skipped
clear
place 1 4 4 500 596
place 4 300 300 300 400
hit 300 350
# reset after a blocking loop consumed the release
clear
place 1 4 4 500 596
place 2 504 4 1000 596
focus 1
route 100 100 2
mreset 0
route 600 100 1
route 600 100 0
# a button still held after the loop is swallowed until release
mreset 1
route 100 100 1
route 100 100 0
route 100 100 1
