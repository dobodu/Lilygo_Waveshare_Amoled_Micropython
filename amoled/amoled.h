#ifndef __AMOLED_H__
#define __AMOLED_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "py/obj.h"
#include "mpfile/mpfile.h"
#include "schrift/schrift.h"
#include "amoled_qspi_bus.h"

#define LCD_CMD_NOP          0x00 // This command is empty command
#define LCD_CMD_SWRESET      0x01 // Software reset registers (the built-in frame buffer is not affected)
#define LCD_CMD_RDDID        0x04 // Read 24-bit display ID
#define LCD_CMD_RDDST        0x09 // Read display status
#define LCD_CMD_RDDPM        0x0A // Read display power mode
#define LCD_CMD_RDD_MADCTL   0x0B // Read display MADCTL
#define LCD_CMD_RDD_COLMOD   0x0C // Read display pixel format
#define LCD_CMD_RDDIM        0x0D // Read display image mode
#define LCD_CMD_RDDSM        0x0E // Read display signal mode
#define LCD_CMD_RDDSR        0x0F // Read display self-diagnostic result
#define LCD_CMD_SLPIN        0x10 // Go into sleep mode (DC/DC, oscillator, scanning stopped, but memory keeps content)
#define LCD_CMD_SLPOUT       0x11 // Exit sleep mode
#define LCD_CMD_PTLON        0x12 // Turns on partial display mode
#define LCD_CMD_NORON        0x13 // Turns on normal display mode
#define LCD_CMD_INVOFF       0x20 // Recover from display inversion mode
#define LCD_CMD_INVON        0x21 // Go into display inversion mode
#define LCD_CMD_GAMSET       0x26 // Select Gamma curve for current display
#define LCD_CMD_DISPOFF      0x28 // Display off (disable frame buffer output)
#define LCD_CMD_DISPON       0x29 // Display on (enable frame buffer output)
#define LCD_CMD_CASET        0x2A // Set column address
#define LCD_CMD_RASET        0x2B // Set row address
#define LCD_CMD_RAMWR        0x2C // Write frame memory
#define LCD_CMD_RAMRD        0x2E // Read frame memory
#define LCD_CMD_PTLAR        0x30 // Define the partial area
#define LCD_CMD_VSCRDEF      0x33 // Vertical scrolling definition
#define LCD_CMD_TEOFF        0x34 // Turns of tearing effect
#define LCD_CMD_TEON         0x35 // Turns on tearing effect

#define LCD_CMD_MADCTL       0x36     // Memory data access control
#define MADCTL_DEFAULT	     0x00	  // Default Value
#define MADCTL_MH_BIT        (1 << 2) // Display data latch order, 0: refresh left to right, 1: refresh right to left
#define MADCTL_BGR_BIT       (1 << 3) // RGB/BGR order, 0: RGB, 1: BGR
#define MADCTL_ML_BIT        (1 << 4) // Line address order, 0: refresh top to bottom, 1: refresh bottom to top
#define MADCTL_MV_BIT        (1 << 5) // Row/Column order, 0: normal mode, 1: reverse mode
#define MADCTL_MX_BIT        (1 << 6) // Column address order, 0: left to right, 1: right to left
#define MADCTL_MY_BIT        (1 << 7) // Row address order, 0: top to bottom, 1: bottom to top

#define LCD_CMD_VSCSAD       0x37 // Vertical scroll start address
#define LCD_CMD_IDMOFF       0x38 // Recover from IDLE mode
#define LCD_CMD_IDMON        0x39 // Fall into IDLE mode (8 color depth is displayed)
#define LCD_CMD_COLMOD       0x3A // Defines the format of RGB picture data
#define LCD_CMD_RAMWRC       0x3C // Memory write continue
#define LCD_CMD_RAMRDC       0x3E // Memory read continue
#define LCD_CMD_SETTSCANL    0x44 // Set tear scanline, tearing effect output signal when display module reaches line N
#define LCD_CMD_GETSCANL     0x45 // Get scanline
#define LCD_CMD_WRDISBV      0x51 // Write display brightness
#define LCD_CMD_RDDISBV      0x52 // Read display brightness value
//Below is specific for SH8601H
#define LCD_CMD_WRCTRLD1     0x53 // Write CTRL Display 1
#define LCD_CMD_RDCTRLD1	 0x54 // Read CTRL Display 1
#define LCD_CMD_WRCTRLD2     0x55 // Write CTRL Display 2
#define LCD_CMD_RDCTRLD2	 0x56 // Read CTRL Display 2
#define LCD_CMD_WRCE	 	 0x57 // Write Cde Sunlight Readability Enhancement
#define LCD_CMD_RDCE	 	 0x58 // Read Cde Sunlight Readability Enhancement
#define LCD_CMD_HBM_WRDISBV	 0x63 // Write Display Brightness Value in HBM Mode
#define LCD_CMD_HBM_RDDISBV  0x64 // Read Display Brightness Value in HBM Mode
#define LCD_CMD_HBMCTL 		 0x66 // HBM Control

#define LCD_CMD_SETHBMMODE	0xB0 // Set High Brightness Mode (only for RM67162)
#define LCD_CMD_SETDISPMODE 0xC2 // Set DSI Mode
#define LCD_CMD_SETSPIMODE  0xC4 // Set DSPI Mode
#define LCD_CMD_SWITCHMODE	0xFE // Switch Command Mode
#define LCD_CMD_READMODE	0xFF // Read Command Status

//RM680B0 and RM67162 factory registers
#define LCD_FAC_OVSSCONTROL 0x05 // OVSS Control
#define LCD_FAC_OVSSVOLTAGE 0x73 // OVSS Control
#define LCD_FAC_MIPI		0x26 // MIPI
#define LCD_FAC_SPI			0x24 // SPI
#define LCD_FAC_SWIRE1		0x5A // SWIRE
#define LCD_FAC_SWIRE2		0x5B // SWIRE

// BPP Colmod 
#define COLMOD_CAL_16   	0x55
#define COLMOD_CAL_18   	0x66
#define COLMOD_CAL_24   	0x77

//BPP Filter 
#define COLMOD_FIL_16   	0x0
#define COLMOD_FIL_18   	0x1
#define COLMOD_FIL_24   	0x2

// Color definitions in 16bpp RGB (litte indian : 1F00 => 001F mean Full Blue)

#define BLACK   0x0000
#define BLUE    0x1F00
#define RED     0x00F8
#define GREEN   0xE007
#define CYAN    0xFF07
#define MAGENTA 0x1FF8
#define YELLOW  0xE0FF
#define WHITE   0xFFFF

#define COLOR_SPACE_RGB        (0)
#define COLOR_SPACE_BGR        (1)
#define COLOR_SPACE_MONOCHROME (2)


typedef struct	_Point					Point;
typedef struct	_Polygon				Polygon;
typedef struct	_amoled_rotation_t		amoled_rotation_t;
typedef struct  _bpp_process_t			bpp_process_t;
typedef struct	_amoled_AMOLED_obj_t	amoled_AMOLED_obj_t;
typedef struct	_IODEV					IODEV;
typedef struct  _acc_Glyphset			acc_Glyphset;
typedef struct  _acc_Glyph				acc_Glyph;

struct _acc_Glyphset {
	uint16_t    char_total;
	uint32_t    *chr_nb;
	acc_Glyph   *chr_glyph;
};

struct _acc_Glyph {
	float		advanceWidth;
	float		leftSideBearing;
	float		yOffset;
	uint8_t		width;
	uint8_t		height;
	//float		xOffset;	
	uint8_t		*pixels;
};

struct _Point {
    mp_float_t x;
    mp_float_t y;
};

struct _Polygon {
    int length;
    Point *points;
};

struct _amoled_rotation_t {
    uint8_t madctl;
    uint16_t width;
    uint16_t height;
    uint16_t colstart;
    uint16_t rowstart;
};

struct _bpp_process_t {
    uint32_t 	fltr_col_rd;
    uint8_t 	bitsw_col_rd;
    uint32_t	fltr_col_gr;
    uint8_t 	bitsw_col_gr;
	uint32_t	fltr_col_bl;
};

struct _amoled_AMOLED_obj_t {
    mp_obj_base_t 		base;
    mp_obj_base_t 		*bus_obj;
    amoled_panel_p_t 	*lcd_panel_p;
    mp_obj_t 			reset;
	bool 				reset_level;	//True fo hard reset
	
	// Memory allocated pointers
    void 		*work;					// work buffer for jpg & png decoding
    uint8_t 	*scanline_ringbuf;  	// png scanline_ringbuf
    uint8_t 	*palette;           	// png palette
    uint8_t 	*trans_palette;     	// png trans_palette
    uint8_t		*gamma_table;       	// png gamma_table		
	uint16_t 	*fram_buf;				// Global Frame buffer
	uint16_t 	*temp_buf;				// Temporary Frame buffer

	// Display parameters
	uint8_t 	type;					// Type 0 : RM67162 / 1 : RM690B0 / 3 : SH8601
	amoled_rotation_t rotations[4];		// list of rotation tuples
    uint8_t		rotation;				// Current rotation
	uint8_t 	madctl_val;				// Current value of LCD_CMD_MADCTL register
    uint8_t 	colmod_cal;				// Current value of LCD_CMD_COLMOD register
    uint16_t 	width;					// Display width
    uint16_t 	height;					// Display height
	uint16_t 	col_start;				// Gap for 1 columns
    uint16_t 	row_start;				// Gap for 1 line
	uint8_t 	color_space;			// 0 : RGB / 1 : BGR / 2 : Mono
    uint8_t 	bpp;					// Diplay bit per pixel : 16 / 18 / 24
    uint8_t 	Bpp;					// Display Byte per pixel : 2 / 3 / 3
	uint8_t		te;						// tearing effect 0/1
	uint16_t    scanline;				// tearing scanline
	bpp_process_t bpp_process;			// bpp process filter and switches

	//Frame Buffer related
    bool 		auto_refresh;           // True => Every action is directly rendered to display
	bool 		hold_display;           // True => skip display refresh until decided
	uint8_t 	bus_methode;            //FOR DEVELOPPEMENT PURPOSE
};

struct _IODEV {
    mp_file_t *fp;              // File pointer for input function
    uint8_t *fbuf;              // Pointer to the frame buffer for output function
    unsigned int wfbuf;         // Width of the frame buffer [pix]
    unsigned int left;          // jpg crop left column
    unsigned int top;           // jpg crop top row
    unsigned int right;         // jpg crop right column
    unsigned int bottom;        // jpg crop bottom row
    amoled_AMOLED_obj_t *self;  // display object
    // for buffer input function
    uint8_t *data;
    unsigned int dataIdx;
    unsigned int dataLen;
};

mp_obj_t amoled_AMOLED_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args);

extern const mp_obj_type_t amoled_AMOLED_type;

#ifdef  __cplusplus
}
#endif

#endif
