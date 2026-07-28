
import random
import utime
import amoled
import fonts.large as font
import gc

BOARD = "WS_241_AMOLED" #LG_191_AMOLED or LG_241_AMOLED or WS_180_AMOLED or WS_241_AMOLED

if BOARD == "LG_191_AMOLED" :
    from config.LG_191_AMOLED import *
elif BOARD == "LG_241_AMOLED" :
    from config.LG_241_AMOLED import *
elif BOARD == "WS_180_AMOLED" :
    from config.WS_180_AMOLED import *
elif BOARD == "WS_241_AMOLED" :
    from config.WS_241_AMOLED import *

POLYGON = [(50,-25),(25,-25),(25,-50),(-25,-50),(-25,-25),(-50,-25),(-50,25),(-25,25),(-25,50),(25,50),(25,25),(50,25),(50,-25)]

fnt = display.ttf_load("/fonts/test.ttf")
display.ttf_scale(fnt, 24, 24)

def main():
    
    print("Press Ctrl-C to stop")
    
    display.reset()
    display.init()
    display.rotation(3)
    display.brightness(0)
    display.jpg("/bmp/smiley_small.jpg",0,0)
    for i in range(255):
        display.brightness(i)
        utime.sleep(0.01)
    utime.sleep(0.5)
    display.fill(amoled.BLACK)
    utime.sleep(0.5)
    
    text = "Hello!"
    write_length = display.write_len(font,text)
    write_height = font.HEIGHT
    ttf_write_length = display.ttf_len(fnt,text)
    ttf_write_height = 24 #Font scale
    
    try : 
        while True:

            for rotation in range(4):
                rotation = 0
                display.rotation(rotation)
                display.fill(amoled.BLACK)
                col_max = display.width()
                row_max = display.height()
                print("Rot", rotation, "Width", col_max, "height", row_max) 
                write_xmax = col_max - write_length
                write_ymax = row_max - write_height
                ttf_write_xmax = col_max - ttf_write_length
                ttf_write_ymax = row_max - ttf_write_height
                
                filled = random.randint(0,1)
                kind = random.randint(0,8)
                
                #For debug or test
                #kind = 0
                #filled = 0
                
                print("mem", gc.mem_free())
                
                
                start_time = utime.ticks_ms()

                for _ in range(128):
                    xpos = random.randint(5, col_max-5)
                    ypos = random.randint(5, row_max-5)
                    length = random.randint(0, col_max - xpos) // 2
                    height = random.randint(0, row_max - ypos) // 2
                    radius = random.randint(1, min(col_max - xpos-1, xpos, row_max - ypos-1, ypos )) // 2
                    radius_1 = random.randint(0, min(col_max - xpos, xpos)) // 2
                    radius_2 = random.randint(0, min(row_max - ypos, ypos)) // 2
                    angle = random.randint(0,628) / 100
                    color = display.colorRGB(
                            random.getrandbits(8),
                            random.getrandbits(8),
                            random.getrandbits(8))
                    color2 = display.colorRGB(
                            random.getrandbits(8),
                            random.getrandbits(8),
                            random.getrandbits(8))
                    
                    if kind == 0 :
                        kind_name = "Pixel"
                        display.pixel(xpos, ypos, color)
                        print("\t", kind_name, "X", xpos,"Y", ypos)
                    
                    if kind == 1 :
                        kind_name = "Circl"
                        if filled :
                            display.fill_circle(xpos,ypos,radius, color)
                        else :
                            display.circle(xpos, ypos, radius, color)
                        print("\t", kind_name, "X", xpos,"Y", ypos,"R", radius)
                            
                    if kind == 2 :
                        kind_name = "Ellipse"
                        if filled :
                            display.fill_ellipse(xpos,ypos,radius_1, radius_2, color)
                        else :
                            display.ellipse(xpos, ypos, radius_1, radius_2, color)
                        print("\t", kind_name, "X", xpos,"Y", ypos,"Rx", radius_1,"Ry", radius_2 )
                    
                    if kind == 3 :
                        kind_name = "Recta"
                        if filled :
                            display.fill_rect(xpos,ypos,length, height, color)
                        else :
                            display.rect(xpos,ypos,length, height, color)
                        print("\t", kind_name, "X", xpos,"Y", ypos,"Len", length,"Hei", height)
                    
                    if kind == 4 :
                        kind_name = "BRect"
                        if filled :
                            display.fill_bubble_rect(xpos,ypos,length, height, color)
                        else :
                            display.bubble_rect(xpos,ypos,length, height, color)
                            
                    if kind == 5 :
                        kind_name = "Trian"
                        if filled :
                            display.fill_trian(random.randint(0, col_max), random.randint(0, row_max),
                                           random.randint(0, col_max), random.randint(0, row_max),
                                           random.randint(0, col_max), random.randint(0, row_max), color)
                        else :
                            display.trian(random.randint(0, col_max), random.randint(0, row_max),
                                           random.randint(0, col_max), random.randint(0, row_max),
                                           random.randint(0, col_max), random.randint(0, row_max), color)
                            
                    if kind == 6 :
                        kind_name = "BITfon"
                        if filled :
                            display.write(font,text, random.randint(10, write_xmax-10), random.randint(10, write_ymax-10), color,color2)
                        else :
                            display.write(font,text, random.randint(10, write_xmax-10), random.randint(10, write_ymax-10), color)
                            
                    if kind == 7 :
                        kind_name = "Poly"
                        if filled :
                            display.fill_polygon(POLYGON, random.randint(55, col_max-55), random.randint(55, row_max-55) , color, angle)
                        else :
                            display.polygon(POLYGON, random.randint(55, col_max-55), random.randint(55, row_max-55) , color, angle)
                            
                    if kind == 8 :
                        kind_name = "TTFfon"
                        if filled :
                            display.ttf_draw(fnt, text, random.randint(10, ttf_write_xmax-10), random.randint(10+ttf_write_height, ttf_write_ymax-10), color,color2)
                        else :
                            display.ttf_draw(fnt, text, random.randint(10, ttf_write_xmax-10), random.randint(10+ttf_write_height, ttf_write_ymax-10), color)
                            

                end_time = utime.ticks_ms()
                fps = 1000*128/(end_time - start_time)
                fps_txt = "Rot {:.0f}-{}-{:.0f}/s".format(rotation,kind_name,fps)
                display.write(font,fps_txt, 0, 0, color)
                print("Rot", rotation, kind_name, "Fiiled", filled,"FPS", fps) 
                utime.sleep(1)
                display.refresh()
                utime.sleep(1)
                
    except KeyboardInterrupt:
        pass
    
    display.ttf_free(fnt)
       
main()
