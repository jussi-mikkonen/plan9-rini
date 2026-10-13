# mouse policy: menu, float move/resize, Mod on tiles
clear
place 1 4 4 500 596
place 2 200 100 400 300 f
place 3 600 100 700 200 f parked
# plain B3: background, gap, border band -> menu; inside client -> pass
mpol 1000 1000 4 1000 0 0
mpol 1000 1000 0 1010 0 0
mpol 502 100 4 1020 0 0
mpol 502 100 0 1030 0 0
mpol 5 100 4 1040 0 0
mpol 5 100 0 1050 0 0
mpol 100 100 4 1060 0 0
mpol 110 100 4 1070 0 0
mpol 110 100 0 1080 0 0
# the parked float is not there
mpol 650 150 4 1090 0 0
mpol 650 150 0 1100 0 0
# Mod+B3 on a tile and on the background -> menu
mpol 100 100 4 1110 1 0
mpol 100 100 0 1120 1 0
mpol 1000 1000 4 1130 1 0
mpol 1000 1000 0 1140 1 0
# Mod+B3 on a float, release without motion -> swallowed, no menu (menuhit needs B3 held)
mpol 250 150 4 1150 1 0
mpol 252 152 4 1160 1 0
mpol 252 152 0 1170 1 0
# Mod+B3 on a float, motion past 4 px -> resize, nearest corner
mpol 380 280 4 1180 1 0
mpol 384 280 4 1190 1 0
mpol 386 280 4 1200 1 0
mpol 386 280 0 1210 1 0
mpol 210 110 4 1220 1 0
mpol 210 120 4 1230 1 0
mpol 210 120 0 1240 1 0
# Mod+B1 on a float -> move drag with offsets, final offset on release
mpol 250 150 1 1300 1 0
mpol 260 160 1 1310 1 0
mpol 270 155 1 1320 1 0
mpol 270 155 0 1330 1 0
mpol 270 155 0 1340 0 0
# Mod+B1 / Mod+B2 on a tile swallow until release
mpol 100 100 1 1400 1 0
mpol 110 100 1 1410 1 0
mpol 110 100 0 1420 1 0
mpol 100 100 2 1430 1 0
mpol 100 100 0 1440 1 0
# no Mod, B1: pass
mpol 100 100 1 1500 0 0
mpol 100 100 0 1510 0 0
# reset with a held button swallows until release
preset 4
mpol 100 100 4 1600 0 0
mpol 100 100 0 1610 0 0
mpol 100 100 4 1620 0 0
