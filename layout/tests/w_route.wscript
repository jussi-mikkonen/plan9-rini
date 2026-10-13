# mouse routing: click-to-focus, B1 swallow, grab until release
clear
place 1 4 4 500 596
place 2 504 4 1000 596
focus 1
# B1 press on the unfocused window focuses it and is swallowed until release
route 600 100 1
route 610 100 1
route 610 100 0
# a press on the focused window grabs it, release still goes to the grab
route 610 100 1
route 100 100 1
route 100 100 0
# motion: focused window, other window, the gap
route 620 100 0
route 100 100 0
route 502 100 0
# B3 on the unfocused window focuses and delivers
route 100 100 4
route 100 100 0
# wheel never changes focus
route 600 100 8
route 600 100 0
route 100 100 8
route 100 100 0
# the background is nobody
route 2 2 1
route 2 2 0
route 2 2 0
