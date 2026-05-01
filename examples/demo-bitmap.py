#
# Animated bitmap example
# Code submitted by phiser678
# See [Issue 5]
#
# Animated bitmap can be created thank to ffmpeg
# command line example
#
# ffmpeg -i movie.mp4 -f rawvideo -pix_fmt rgb565be -vframes 50 -s 102x160 ani.565


import utime
import amoled
import fonts.large as font

BOARD = "LG_191_AMOLED" #LG_191_AMOLED or LG_241_AMOLED or WS_180_AMOLED or WS_241_AMOLED

if BOARD == "LG_191_AMOLED" :
    from config.LG_191_AMOLED import *
elif BOARD == "LG_241_AMOLED" :
    from config.LG_241_AMOLED import *
elif BOARD == "WS_180_AMOLED" :
    from config.WS_180_AMOLED import *
elif BOARD == "WS_241_AMOLED" :
    from config.WS_241_AMOLED import *

def main():
    ani=[]
    iw=102 # image width
    ih=160 # image height
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
    display.reset()
    display.init()
    display.rotation(0)
    display.brightness(100)
    display.fill(amoled.BLACK)
    corner=display.width()-iw
    middle=display.height()//2
    color=display.colorRGB(0,254,0)

    try :
        while True:
            start_time = utime.ticks_ms()
            for i in range(0,frames):
                display.bitmap(corner,0,corner+iw-1,ih-1,ani[i])
                # comment next line for full speed
                utime.sleep(0.038) # or get 25 fps 
            end_time = utime.ticks_ms()
            fps = 1000*45/(end_time - start_time)
            display.write(font, f"    fps=%.03f" % fps, 0, middle, color, 0)
                
    except KeyboardInterrupt:
        pass
    
    display.deinit()
       
main()
