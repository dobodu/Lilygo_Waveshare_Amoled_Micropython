
## Lilygo and Waveshare Amoled Series Micropython firmware with Graphic Support
**NB : this is a totally different project from LVGL**

--------------------------------------------------------------------------------------------------

**Updated 05/01/2026 : ARTEFACTS GONE AWAY...**

**Updated 30/11/2025 : NOW WORKS WITH TRUETYPE FONTS THANKS TO LIBSCHRIFT INTEGRATION**

![TTF](https://github.com/dobodu/Lilygo_Waveshare_Amoled_Micropython/blob/main/Images/ttf_thumb.jpg?raw=true)
![variable font](https://github.com/dobodu/Lilygo_Waveshare_Amoled_Micropython/blob/main/Images/variable_font_thumb.jpg?raw=true)
![filled variable font](https://github.com/dobodu/Lilygo_Waveshare_Amoled_Micropython/blob/main/Images/filled_variable_font_thumb.jpg?raw=true)
![rectangles](https://github.com/dobodu/Lilygo_Waveshare_Amoled_Micropython/blob/main/Images/rectangles_thumb.jpg?raw=true)
![circles](https://github.com/dobodu/Lilygo_Waveshare_Amoled_Micropython/blob/main/Images/circles_thumb.jpg?raw=true)
![filled circles](https://github.com/dobodu/Lilygo_Waveshare_Amoled_Micropython/blob/main/Images/filled_circles_thumb.jpg?raw=true)
![filled triangles](https://github.com/dobodu/Lilygo_Waveshare_Amoled_Micropython/blob/main/Images/filled_triangles_thumb.jpg?raw=true)
![polygons](https://github.com/dobodu/Lilygo_Waveshare_Amoled_Micropython/blob/main/Images/polygons_thumb.jpg?raw=true)
![filled polygons](https://github.com/dobodu/Lilygo_Waveshare_Amoled_Micropython/blob/main/Images/filled_polygons_thumb.jpg?raw=true)
![jpeg](https://github.com/dobodu/Lilygo_Waveshare_Amoled_Micropython/blob/main/Images/jpeg_thumb.jpg?raw=true)

Above examples where established with a Waveshare AMOLED 1.8", Top corner values are Nb of objects redered every second...

[See the demo on Youtube](https://www.youtube.com/watch?v=m3pqW5jGypQ)

--------------------------------------------------------------------------------------------------

**This graphic library is dedicated to :**

- Lilygo T4-S3 AMOLED
- Lilygo T-Display S3 AMOLED
- Lilygo 1.43 inches SH8601 AMOLED
- Lilygo 1.75 inches SH8601 AMOLED
- Waveshare ESP32-S3 1.8 inches AMOLED Touch
- Waveshare ESP32-S3 2.41 inches AMOLED Touch

This Micropython driver is created on behalf of [nspsck](https://github.com/nspsck/RM67162_Micropython_QSPI) RM67162 driver.
It is also convergent with [russhugues](https://github.com/russhughes/st7789_mpy) ST7789 driver.
I also would like to thanks [lewisxhe](https://github.com/Xinyuan-LilyGO/LilyGo-AMOLED-Series). Your advices helped me a lot.
Finally, I must talk of [tomolt](https://github.com/tomolt/libschrift). The libschrift TTF lightweight library truelly rocks !


My main goal was to adapt a driver library that would give the same functions thant ST7789 driver, in order to
be able to get my micropythons projects working whether on PICO + ST7789 or ESP32 + RM690B0 or ESP32 + RM67162

The driver involves a frame buffer of 600x450, requiring 540ko of available ram (T4-S3 version).
The driver involves a frame buffer of 536x240, requiring 280ko of available ram (TDisplay S3 version).
In a more general way, requirements are WIDTH x HEIGHT x 2 bytes or ram.

Latest firmware is build with
- Micropython 1.26.1
- ESP IDF toolchain 5.5.2


Contents:

- [Lilygo Amoled Series Micropython firmware with Graphic Support](#Lilygo)
- [Introduction](#introduction)
- [Features](#features)
- [Documentation](#documentation) 
- [How to build](#build)
- [Optional Scripts](#optional-scripts)



## Introduction
This is the successor of the previous [lcd_binding_micropython](https://github.com/nspsck/lcd_binding_micropython). 
It is reconstructed to be more straightforward to develop on, and this allows me to test the changes before committing.

This driver is based on [esp_lcd](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/lcd.html).

Available functions: `fill, fill_rect, rect, fill_circle, circle, pixel, vline, hline, colorRGB, bitmap, brightness, line, text, text_len, write, write_len` etc.
For full details please visit [the documentation](#documentation).

Fixed spacing font was created by russhughes.
Variable spacing font was created by myself

The firmware is provided each time when I update this repo. 

## Working : 
- text         monosized font
- text_len
- write        variable spacing font
- write_len
- draw         hershey vectorial font
- draw_len
- fill
- rect
- fill_rect
- trian
- fill_tria
- bubble_rect
- fill_bubble_rect
- circle
- fill_circle
- ellipse * New
- fill_ellipse *New
- polygons
- fill_polygons
- fast_hline
- fast_vline
- line
- jpg         display JPG image
- brightness
- rotation
- and so on...

## to check : 
- As far as I know everything is working as expected

## To-DO List : 
- add buffered True Type Font in order to accelerate the display rendering when always using the same size font

## Features

The following display driver ICs are supported:
- RM690B0
- RM67162
- SH8601 

Supported boards：
- [LILYGO T4 S3 AMOLED](https://www.lilygo.cc/products/t4-s3)
- [LILYGO T-DISPLAY S3 AMOLED](https://www.lilygo.cc/products/t-display-s3-amoled)
- [WAVESHARE ESP32-S3 1.8 AMOLED](https://www.waveshare.com/esp32-s3-touch-amoled-1.8.htm)
- [WAVESHARE ESP32-S3 2.41 AMOLED](https://www.waveshare.com/esp32-s3-touch-amoled-2.41.htm)

| Driver IC | Display IC |    SPI    |   QSPI    |   I8080   |   DPI     |
| --------- | ---------- | --------- | --------- | --------- | --------- |
| ESP32-S3  |  RM690B0   |    NO     |   YES     |    NO     |    NO     |
| ESP32-S3  |  RM67162   |    NO     |   YES     |    NO     |    NO     |
| ESP32-S3  |  SH8601    |    NO     |   YES     |    NO     |    NO     |
| ESP32-S3  |  CO5300 *  |    NO     |   YES     |    NO     |    NO     |

*very early support (not tested yet)

Demonstration Video :
- [Amoled Micropython Demo](https://www.youtube.com/watch?v=m3pqW5jGypQ)

## Declaration

Given exemple is working on the Waveshare ESP32-S3 AMOLED 2.41"

Beware, sometime the display needs to be awaken by a specific pin (EXI01 AMOLED_EN for this specific device). A micropython developped by myself if provided in the example directory if needed.

```Shell
import amoled

SPI_PORT = 2
SPI_BAUD = 80_000_000
SPI_POLAR = False
SPI_PHASE = False

FT_CS  = Pin(9,Pin.OUT)   # CHIP SELECT
TFT_SCK = Pin(10,Pin.OUT)   # SPICLK_P
TFT_MOSI = None
TFT_MISO = None
TFT_RST = Pin(21,Pin.OUT)   # TFT RESET
TFT_D0  = Pin(11,Pin.OUT)   # D0 QSPI
TFT_D1  = Pin(12,Pin.OUT)   # D1 QSPI
TFT_D2  = Pin(13,Pin.OUT)   # D2 QSPI & SPICLK_N
TFT_D3  = Pin(14,Pin.OUT)   # D3 QSPI

TFT_WIDTH = 600
TFT_HEIGHT = 450

spi = SPI(SPI_PORT, baudrate = SPI_BAUD, sck=TFT_SCK, mosi=TFT_MOSI, miso=TFT_MISO, polarity=SPI_POLAR, phase=SPI_PHASE)
panel = amoled.QSPIPanel(spi=spi, data=(TFT_D0, TFT_D1, TFT_D2, TFT_D3),
            dc=TFT_D1, cs=TFT_CS, pclk=SPI_BAUD, width=TFT_HEIGHT, height=TFT_WIDTH)
display = amoled.AMOLED(panel, type=1, reset=TFT_RST, bpp=16, auto_refresh= True, bus_methode=0)
```
Mandatory parameters are : panel and type,
Optional parameters are : reset, bpp, auto_refresh, bus_methode
especially bus_methode is a dev parameter since I try to improve (+15%) the display rendering but
I face artefacts probably du to internal SPI/Cache of ESP32 behaviour (bus_methode=1 or 2...)

Example for using GPIO extender for waveshare Amoled 1.8"

    import tca9554  # Extended GPIO
    [...]
    TFT_CDE = tca9554.TCA9554(i2c, pin=1, io=0) #Specific EXI01 AMOLED_EN : Output
    
    TFT_TS_RST = tca9554.TCA9554(i2c, pin=2, io=0) # TFT RST is EXIO_2
    panel = amoled.QSPIPanel(spi=spi, data=(TFT_D0, TFT_D1, TFT_D2, TFT_D3),
            dc=TFT_D1, cs=TFT_CS, pclk=80_000_000, width=TFT_WIDTH, height=TFT_HEIGHT)
            
    display = amoled.AMOLED(panel, type=2, reset=TFT_RST, bpp=16)
    display.reset()
    display.init()
    display.brightness(255)
    TFT_CDE.value(1)
    
The last line TFT_CDE.value will activate the display...

    

## Documentation
In general, the screen starts at 0 and goes to 599 x 449 for T4-S3 (resp 535 x 239 for T-Display S3), that's a total resolution of 600 x 450 (resp 536 x 240).
All drawing functions should be called with this in mind.

```Shell
dir(amoled)
['__class__', '__name__', 'AMOLED', 'BGR', 'BLACK', 'BLUE', 'CYAN', 'GREEN', 'MAGENTA', 'MONOCHROME',
 'QSPIPanel', 'RED', 'RGB', 'WHITE', 'YELLOW', '__dict__']

dir(amoled.AMOLED)
['__class__', '__name__', 'write', 'BGR', 'MONOCHROME', 'RGB', '__bases__',
 '__del__', '__dict__', 'backlight_off', 'backlight_on', 'bitmap',
 'brightness', 'bubble_rect', 'circle', 'colorRGB', 'deinit', 'disp_off',
 'disp_on', 'draw', 'draw_len', 'fill', 'fill_bubble_rect', 'fill_circle',
 'fill_polygon', 'fill_rect', 'fill_trian', 'height', 'hline', 'init',
 'invert_color', 'jpg', 'jpg_decode', 'line', 'mirror', 'pixel', 'polygon',
 'polygon_center', 'rect', 'refresh', 'reset', 'rotation', 'send_cmd',
 'set_gap', 'swap_xy', 'text', 'text_len', 'trian', 'ttf_draw',
 'ttf_init_font', 'ttf_len', 'ttf_load_font', 'ttf_scale_font', 'version',
 'vline', 'vscroll_area', 'vscroll_start', 'width', 'write_len']
```

- `amoled.COLOR`

  This returns a predefined color that can be directly used for drawing. Available options are: BLACK, BLUE, RED, GREEN, CYAN, MAGENTA, YELLOW, WHITE

- `init()`

  Must be called to initialize the display.

- `deinit()`

  Deinit the tft object and release the memory used for the framebuffer.

- `reset()`

  Soft reset the display.

- `rotation(value)`

  Rotate the display, value range: 0 - 3.

- `tearing(tearing value, [scanline value])`

  Set tearing control (0 = off, 1 = on).
  Scanline value is optional (defaut = height of the display).

- `brightness(value)`

  Set the screen brightness, value range: 0 - 100, in percentage.

- `disp_off()`

  Turn off the display.

- `disp_on()`

  Turn on the display.

- `backlight_on()`

  Turn on the backlight, this is equal to `brightness(100)`.

- `backlight_off()`

  Turn off the backlight, this is equal to `brightness(0)`.

- `refresh([x0,y0,x1,y1])`

  Force refresh of the screen
  The screen is entirely refresh if no arguments are passed through, otherwise only specifyed area is refreshed
  Usefull when parameter auto_refresh=false has been used during the display declaration.

- `invert_color()`

  Invert the display color.

- `height()`

  Returns the height of the display.

- `width()`

  Returns the width of the display.

- `colorRGB(r, g, b)`

  Call this function to get the rgb color for the drawing.

- `pixel(x, y, color)`

  Draw a single pixel at the position (x, y) with color.

- `hline(x, y, l, color)`

  Draw a horizontal line starting at the position (x, y) with color and length l. 

- `vline(x, y, l, color)`

  Draw a vertical line starting at the position (x, y) with color and length l.

  - `line(x0, y0, x1, y1, color)`

  Draw a line (not anti-aliased) from (x0, y0) to (x1, y1) with color.

- `fill(color)`

  Fill the entire screen with the color.

- `fill_rect(x, y, w, h, color)`

  Draw a rectangle starting from (x, y) with the width w and height h and fill it with the color.

- `rect(x, y, w, h, color)`

  Draw a rectangle starting from (x, y) with the width w and height h of the color.

  - `fill_trian(x1, y1, x2, y2, x3, y3, color)`

  Draw a triangle starting from fill it with the color.

- `trian(x1, y1, x2, y2, x3, y3, color)`

  Draw a triangle starting of the color without filling it.

- `fill_bubble_rect(x, y, w, h, color)`

  Draw a rounded text-bubble-like rectangle starting from (x, y) with the width w and height h and fill it with the color.

- `bubble_rect(x, y, w, h, color)`

  Draw a rounded text-bubble-like rectangle starting from (x, y) with the width w and height h of the color.

- `fill_circle(x, y, r, color)`

  Draw a circle with the middle point (x, y) with the radius r and fill it with the color.

- `circle(x, y, r, color)`

  Draw a circle with the middle point (x, y) with the radius r of the color.

  - `fill_ellipse(x, y, rx, ry, color)`

  Draw an ellipse with the middle point (x, y) with radius rx & ry and fill it with the color.

- `ellipse(x, y, rx, ry, color)`

  Draw an ellipse with the middle point (x, y) with the radius rx & ry of the color.

- `bitmap(x0, y0, x1, y1, buf)`

  Bitmap the content of a bytearray buf filled with color565 values starting from (x0, y0) to (x1, y1). Currently, the user is responsible for the provided buf content.

- `text(font, text, x, y, fg_color, bg_color)`

  Write text using bitmap fonts starting at (x, y) using foreground color `fg_color` and background color `bg_color`.

- `text_len(bitmap_font, s)`

  Returns the string's width in pixels if printed in the specified font.

- `write(bitmap_font, s, x, y[, fg, bg, background_tuple, fill_flag])`

  Write text to the display using the specified proportional or Monospace bitmap font module with the coordinates as the upper-left corner of the text. The foreground and background colors of the text can be set by the optional arguments `fg` and `bg`, otherwise the foreground color defaults to `WHITE` and the background color defaults to `BLACK`.

  The `font2bitmap` utility creates compatible 1 bit per pixel bitmap modules from Proportional or Monospaced True Type fonts. The character size, foreground, background colors, and characters in the bitmap module may be specified as parameters. Use the -h option for details. If you specify a buffer_size during the display initialization, it must be large enough to hold the widest character (HEIGHT * MAX_WIDTH * 2).

  For more information please visit: [https://github.com/nspsck/st7735s_WeAct_Studio_TFT_port/tree/main](https://github.com/nspsck/st7735s_WeAct_Studio_TFT_port/tree/main)

- `write_len(bitap_font, s)`
  Returns the string's width in pixels if printed in the specified font.

- `draw(vector_font, s, x, y[, fg, scale])`
  Draw text to the display using the specified Hershey vector font with the coordinates as the lower-left corner of the text. The foreground color of the text can be set by the optional argument fg.

- `draw_len(vector_font, s[, scale]`
  Returns the string's width in pixels if drawn with the specified font.

- `ttf_draw(ttf_font, s,x,y,[fg_color, bg_color])`
  Displays the string s, at coordonates x,y. Defaults front color is white but can be defined. If no background color is given, the render will keep current background, otherwise it will use de given background color. Keep in mind that every caracter has its own dimension so the backgroung might be heterogenous (a small letter might be 32x32 whereas it's neighbour might be 32x64, in this case the upper background of the small letter is not rendered). I'll keep improving later.

- `display.ttf_len(ttf_font,s)`
  Gives the width of the string...

For ttf font you have to declare

  - `ttf_font = amoled.TTF(ttf="path_to_ttf_font.ttf", xscale = xx, yscale = yy, kerning = true/false)`
  Create a font object depending on the path file given, defaut scales are 16, and kerning correction to shorten "VA" space, is true by default 

  - `ttf_font.scale(xscale, yscale)`
  Allows to resize font directly

- `ttf_font.deinit()`
  Will release font


## Related Repositories

- [framebuf-plus](https://github.com/lbuque/framebuf-plus)


## Build
This is only for reference. Since ESP-IDF v5.0.2 to v5.4.1, the path to the cmake files changes.
The instruction below are working with ESP-IDF v5.5.2 and Micropython 1.26.

```Shell
cd ~
git clone https://github.com/dobodu/Lilygo_Waveshare_Amoled_Micropython.git

# to the micropython directory
cd micropython/port/esp32
make  BOARD=ESP32_GENERIC_S3 BOARD_VARIANT=FLASH_16M_SPIRAM_OCT USER_C_MODULES=~/Lilygo_Waveshare_Amoled_Micropython CFLAGS_EXTRA=-DMODULE_AMOLED_ENABLED=1
```
You may also want to modify the `sdkconfig` before building in case to get the 16MB storage.
```Shell
cd micropython/ports/esp32
# use the editor you prefer
vim boards/ESP32_GENERIC_S3/sdkconfig.board 
```
Change it to:
```Shell
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_ESPTOOLPY_AFTER_NORESET=y

CONFIG_ESPTOOLPY_FLASHSIZE_4MB=
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions-16MiB.csv"
```

Or simply copy the files provides in the directory ESP32_GENERIC_S3 to you micropython ports/esp32/boards/ESP32_GENERIC_S3
and to ports/esp32 (partitions-16MiB.csv).


If the esp_lcd related functions are missing, do the following:
```Shell
cd micropython/port/esp32
# use the editor you prefer
vim esp32_common.cmake
```
Jump to section `APPEND IDF_COMPONENTS` and add `esp_lcd` to the list should fix this.


# Note: 
Scrolling does not work. Maybe using a framebuffer (provided by Micropython) to scroll will work.

















