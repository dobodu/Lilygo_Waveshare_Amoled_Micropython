from machine import Pin, I2C, SPI
import amoled   # Screen driver

TFT_SD0 = Pin(8,Pin.OUT)   # SERIAL OUTPUT SIGNAL
TFT_TE  = Pin(9,Pin.OUT)   # TEARING EFFET CONTROL
TFT_CS  = Pin(06,Pin.OUT)   # CHIP SELECT
TFT_SCK = Pin(47,Pin.OUT)   # SPICLK_P
TFT_MOSI = None
TFT_MISO = None
TFT_RST = Pin(17,Pin.OUT)   # RESET
TFT_D0  = Pin(18,Pin.OUT)   # D0 QSPI
TFT_D1  = Pin(7,Pin.OUT)   # D1 QSPI
TFT_D2  = Pin(48,Pin.OUT)   # D2 QSPI & SPICLK_N
TFT_D3  = Pin(5,Pin.OUT)   # D3 QSPI
TFT_CDE = Pin(38,Pin.OUT)   #GPIO38 NEEDED FOR SCREEN ON
I2C0_SCL = Pin(2)          #TOUCH I2C SCL 
I2C0_SDA = Pin(3)          #TOUCH I2C SCA

TFT_WIDTH = 536
TFT_HEIGHT = 240
I2C_FREQ = 400_000
SPI_PORT = 2
SPI_BAUD = 80_000_000
SPI_POLAR = False
SPI_PHASE = False
i2c = I2C(0, scl=I2C0_SCL, sda=I2C0_SDA, freq=I2C_FREQ, timeout=200000)
spi = SPI(SPI_PORT, baudrate = SPI_BAUD, sck=TFT_SCK, mosi=TFT_MOSI, miso=TFT_MISO, polarity=SPI_POLAR, phase=SPI_PHASE)
panel = amoled.QSPIPanel(spi=spi, data=(TFT_D0, TFT_D1, TFT_D2, TFT_D3),
            dc=TFT_D1, cs=TFT_CS, pclk=80_000_000, width=TFT_HEIGHT, height=TFT_WIDTH)
display = amoled.AMOLED(panel, type=0, reset=TFT_RST, bpp=16)
display.reset()
display.init()
display.brightness(0)
TFT_CDE.value(1)