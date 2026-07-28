
import random
import utime
import amoled
from math import cos,sin

BOARD = "WS_241_AMOLED" #LG_191_AMOLED or LG_241_AMOLED or WS_180_AMOLED or WS_241_AMOLED

if BOARD == "LG_191_AMOLED" :
    from config.LG_191_AMOLED import *
elif BOARD == "LG_241_AMOLED" :
    from config.LG_241_AMOLED import *
elif BOARD == "WS_180_AMOLED" :
    from config.WS_180_AMOLED import *
elif BOARD == "WS_241_AMOLED" :
    from config.WS_241_AMOLED import *


fnt = amoled.TTF(ttf="/fonts/test.ttf",xscale=32, yscale=32)

text = "Hello!"

display.reset()
display.init()
display.rotation(0)
display.brightness(255)

width = display.width()
height = display.height()

for x in range (256) :
    fnt.scale(min(x,128))
    len=display.ttf_len(fnt, text)
    xpos = (width - len) // 2
    ypos = height // 2
    color = display.colorRGB(random.randint(0,x),random.randint(256-x,256),random.randint(x,256))
    display.ttf_draw(fnt,text,xpos+round(5*cos(x)),ypos+round(5*sin(x)),color)
    print(x)

fnt.deinit()
display.deinit()
