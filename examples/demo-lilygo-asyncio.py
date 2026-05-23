#
# Lilygo T-Display S3 AMOLED example
# Code submitted by phiser678
#
# Animated bitmap can be created thanks to ffmpeg command line, example:
# ffmpeg -i movie.mp4 -f rawvideo -pix_fmt rgb565be -vframes 45 -s 102x160 ani.565
#
# ttf to python font can be created with font2bitmap.py, see
# https://github.com/russhughes/st7789_mpy/blob/master/utils/font2bitmap.py
#
# Arial font  https://github.com/kavin808/arial.ttf
# convert to size 36: font2bitmap.py -c 0x20-0x7A arial.ttf 36
#
# The Lilygo T-Display S3 AMOLED has it's batterylevel connected to ADC Pin 4
# and the green LED is connected to Pin 38
#
# copy animation/ani.565, fonts/arial.py and config/LG_191_AMOLED.py to your Lilygo
# copy this file as main.py and use the reset button to start

import amoled
import asyncio
import machine
import fonts.arial as font

BOARD = "LG_191_AMOLED" #LG_191_AMOLED or LG_241_AMOLED or WS_180_AMOLED or WS_241_AMOLED

if BOARD == "LG_191_AMOLED" :
    from config.LG_191_AMOLED import *
elif BOARD == "LG_241_AMOLED" :
    from config.LG_241_AMOLED import *
elif BOARD == "WS_180_AMOLED" :
    from config.WS_180_AMOLED import *
elif BOARD == "WS_241_AMOLED" :
    from config.WS_241_AMOLED import *

content=["MySSID","MyUsername","MyPassword"]
bat=machine.ADC(Pin(4))
led=machine.Pin(38, Pin.OUT)

# Load images into memory
ani=[]
iw=102 # specify the correct image width
ih=160 # specify the correct image height
frames=0
with open('/animation/ani.565', 'rb') as f:
    while True:
        block = f.read(iw*ih*2)
        if not block:
            break
        ani.append(block)
        frames+=1
        print(f"Loading frame #%d" % frames)

print("Press Ctrl-C to stop")

# Setup display
display.reset()
display.init()
display.rotation(0)
display.brightness(100)
display.fill(amoled.BLACK)
width=display.width()
height=display.height()

white=display.colorRGB(254,254,254)
orange=display.colorRGB(254,164,0)
blue=display.colorRGB(0,149,254)
green=display.colorRGB(0,254,0)

voff=45 # vertical offset
display.write(font,"Wifi Guest", 0, 4, white, 0)
display.rect(0,voff+45,width,2,white) #draw a 2 pixel line with rect
display.rect(0,voff+135,width,2,white)
display.rect(0,voff+225,width,2,white)
display.rect(0,voff+445,width,2,white)
display.rect(0,voff+395,140,voff+395,white)
display.write(font,"SSID", 0, voff+10, blue, 0)
display.write(font,"USERNAME", 0, voff+100, blue, 0)
display.write(font,"PASSWORD", 0, voff+190, blue, 0)
display.write(font,"BAT", 0, voff+360, blue, 0)
display.write(font,content[0], 0, voff+50, orange, 0)
display.write(font,content[1], 0, voff+140, orange, 0)
display.write(font,content[2], 0, voff+230, orange, 0)

# Show animation
async def show_ani():
    global frame
    frame=0
    while True:
        display.bitmap(width-iw,voff+280,width-1,voff+280+ih-1,ani[frame])
        frame+=1
        if frame>=frames:
            frame=0
        await asyncio.sleep_ms(40) # get 25 fps 

# Show batterylevel
async def show_bat():
    while True:
        vbat=(bat.read()* 2 * 3.3 * 1000 / 4096)/1000
        display.write(font,f"%.03f" % vbat, 0, 42+400, orange, 0)
        await asyncio.sleep(1) # batterylevel is shown every second

# Blink LED
async def show_led():
    global blink
    blink=False
    while True:
        led.value(blink)
        blink=not blink
        await asyncio.sleep_ms(250) # LED is blinking 2/s

# Blink bar
async def show_bar():
    global step,color
    step=0
    color=[orange,green]
    while True:
        display.fill_rect(0,height-30,width//2,30,color[step&1]);
        display.fill_rect(width//2,height-30,width//2,30,color[step&2==2]);
        step+=1
        print(step)
        await asyncio.sleep_ms(500) # LED is blinking 1/s


# Main async function
async def main():
    # Run receive and stats tasks concurrently
    await asyncio.gather(show_bar(),show_led(),show_bat(),show_ani())

# Run the async programs
try:
    asyncio.run(main())
except KeyboardInterrupt:
    print("Stopping ...")
    display.deinit()
