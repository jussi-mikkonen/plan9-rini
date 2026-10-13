# wheel policy: Mod+wheel workspace with cooldown, Mod+Shift+wheel column
clear
place 1 4 4 500 596
# Mod+wheel up, release swallowed
mpol 100 100 8 1000 1 0
mpol 100 100 0 1010 1 0
# 100 ms later: cooldown
mpol 100 100 8 1100 1 0
mpol 100 100 0 1105 1 0
# 149 ms after the accepted notch: still cooldown; 150 ms: accepted
mpol 100 100 16 1149 1 0
mpol 100 100 0 1149 1 0
mpol 100 100 16 1150 1 0
mpol 100 100 0 1160 1 0
# Mod+Shift+wheel: column focus, no cooldown
mpol 100 100 16 1170 1 1
mpol 100 100 0 1171 1 1
mpol 100 100 8 1172 1 1
mpol 100 100 0 1173 1 1
# plain wheel passes through, release too
mpol 100 100 8 1200 0 0
mpol 100 100 0 1210 0 0
mpol 100 100 16 1220 0 1
mpol 100 100 0 1230 0 1
# release after the mod was dropped mid-notch is still swallowed
mpol 100 100 8 1400 1 0
mpol 100 100 0 1410 0 0
