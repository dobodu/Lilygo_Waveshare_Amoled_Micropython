/* WAVESHARE and LILYGO AMOLED DRIVER FOR RM67162, RM690B0 and SH8601 

By Dobodu on behalf of 

RussHugues ST7789.mpy library
https://github.com/russhughes/st7789_mpy

Nspsck RM67162_Micropython_QSPI 
https://github.com/nspsck/RM67162_Micropython_QSPI

Xinyuan-LilyGO LilyGo-AMOLED-Series
https://github.com/Xinyuan-LilyGO/LilyGo-AMOLED-Series

Thomas Oltmann Libschrift TTF library
https://github.com/tomolt/libschrift

This micropython C library is a standalone graphic library for

Lilygo T-Display S3 Amoled 1.91"
Lilygo T4-S3 Amoled 2.4"
Waveshare ESP32-S3 Touch Amoled 1.8"
Waveshare ESP32-S3 Touch Amoled 2.41"

License if public

Known limitations

A major part of the code works for 16bpp colorset, event some other are already ok for 18 & 24bpp
*/

#include "amoled.h"
#include "amoled_qspi_bus.h"

#include "py/obj.h"
#include "py/runtime.h"
#include "mphalport.h"
#include "py/gc.h"
#include "py/objstr.h"

#include "esp_lcd_panel_io.h"
#include "driver/spi_master.h"

#include "esp_heap_caps.h"
#include "mpfile/mpfile.h"
#include "jpg/tjpgd565.h"
#include "schrift/schrift.h"

#include <string.h>
#include <math.h>
#include <wchar.h>

#define AMOLED_DRIVER_VERSION "04.01.2026"

#define SWAP16(a, b) { int16_t t = a; a = b; b = t; }
#define ABS(N) (((N) < 0) ? (-(N)) : (N))
#define mp_hal_delay_ms(delay) (mp_hal_delay_us(delay * 1000))

#define MAX_POLY_CORNERS 32

#define MAX_BUFFER  4800

enum { SrcMapping, SrcUser };

const char* color_space_desc[] = {
    "RGB",
    "BGR",
    "MONOCHROME"
};

/* Rotation memento (for RM690B0 and RM67162, SH8601 does not support it)

# = USB PORT
 
   +-----+  +----+  +---#-+  +----+
   |  1  |  |  2 |  |  3  |  # 4  |            
   +-#---+  |    #  +-----+  |    |
            +----+           +----+
*/

// Rotation Matrix { madctl, width, height, colstart, rowstart }
static const amoled_rotation_t ORIENTATIONS_RM690B0[4] = {
    { MADCTL_DEFAULT,									450, 600, 16, 0},
    { MADCTL_DEFAULT | MADCTL_MX_BIT | MADCTL_MV_BIT,	600, 450, 0, 16}, // Column decrease + Row-Col exchange
    { MADCTL_DEFAULT | MADCTL_MX_BIT | MADCTL_MY_BIT,	450, 600, 16, 0}, // Column decrease + Row decrease
    { MADCTL_DEFAULT | MADCTL_MV_BIT | MADCTL_MY_BIT,	600, 450, 0, 16}  // Row decrease + Row-Col exchange
};

static const amoled_rotation_t ORIENTATIONS_RM67162[4] = {
    { MADCTL_DEFAULT,									240, 536, 0, 0},
    { MADCTL_DEFAULT | MADCTL_MX_BIT | MADCTL_MV_BIT, 	536, 240, 0, 0},
    { MADCTL_DEFAULT | MADCTL_MX_BIT | MADCTL_MY_BIT, 	240, 536, 0, 0},
    { MADCTL_DEFAULT | MADCTL_MV_BIT | MADCTL_MY_BIT, 	536, 240, 0, 0}
};

static const amoled_rotation_t ORIENTATIONS_SH8601[4] = {
    { MADCTL_DEFAULT,					368, 448, 0, 0},
    { MADCTL_DEFAULT | MADCTL_MX_BIT, 	368, 448, 0, 0}, //Flipped
    { MADCTL_DEFAULT, 					368, 448, 0, 0},
    { MADCTL_DEFAULT | MADCTL_MX_BIT, 	368, 448, 0, 0}
};

//CO5300 Under developpement
static const amoled_rotation_t ORIENTATIONS_CO5300[4] = {
    { MADCTL_DEFAULT,                                 466, 466, 0, 0},
    { MADCTL_DEFAULT | MADCTL_MX_BIT,                 466, 466, 0, 0}, //Flipped X
    { MADCTL_DEFAULT | MADCTL_MX_BIT |MADCTL_MY_BIT	  466, 466, 0, 0}, // 180°
    { MADCTL_DEFAULT | MADCTL_MY_BIT,                 466, 466, 0, 0}  //Flipped 7
};


/* for each : fltr_col_rd, bitsw_col_rd, fltr_col_gr, bitsw_col_gr, fltr_col_bl
16bpp = 2 bytes continuous 5 bits RED, 6 bits GREEN and 5 bits BLUE
18bpp = 3 bytes discontinuous 6bit/Bytes RGB
24bpp = 3 bytes continuous 8bit/Bytes RBG
Bit transmission is LSB first and MSB then !*/

//Library for now only works with 16BPP
static const bpp_process_t BPP_PROCESS_GEN[3] = {
    { 0xF800, 11, 0x07E0, 5, 0x001F},		//16bpp
    { 0x3F0000, 16, 0x003F00, 8, 0x00003F}, //18bpp
    { 0xFF0000, 16, 0x00FF00, 8, 0x0000FF}, //24bpp
};

int mod(int x, int m) {
    int r = x % m;
    return (r < 0) ? r + m : r;
}

int max_val(uint16_t x1, uint16_t x2) {
	return (x1 > x2) ? x1 : x2;
}

int min_val(uint16_t x1, uint16_t x2) {
	return (x1 < x2) ? x1 : x2;
}


/*----------------------------------------------------------------------------------------------------
Below are Bus transmission related functions.
-----------------------------------------------------------------------------------------------------*/


// send a buffer to the panel display memory using the panel tx_color
static void write_color(amoled_AMOLED_obj_t *self, const void *buf, int len) {
    if (self->lcd_panel_p) {
            self->lcd_panel_p->tx_color(self->bus_obj, 0, buf, len);
    } else {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("Failed to find the panel object."));
    }
}

// send a buffer to the panel IC register using the panel tx_color
static void write_spi(amoled_AMOLED_obj_t *self, int cmd, const void *buf, int len) {
    if (self->lcd_panel_p) {
            self->lcd_panel_p->tx_param(self->bus_obj, cmd, buf, len);
    } else {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("Failed to find the panel object."));
    }
}

// send a command directly from micropython to display
static mp_obj_t amoled_AMOLED_send_cmd(size_t n_args, const mp_obj_t *args)
{
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint8_t cmd = mp_obj_get_int(args[1]);
    uint8_t c_bits = mp_obj_get_int(args[2]);
    uint8_t len = mp_obj_get_int(args[3]);

    if (len <= 0) {
        write_spi(self, cmd, NULL, 0);
    } else {
        write_spi(self, cmd, (uint8_t[]){c_bits}, len);
    }

    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_send_cmd_obj, 4, 4, amoled_AMOLED_send_cmd);


static void set_area(amoled_AMOLED_obj_t *self, uint16_t SC, uint16_t SR, uint16_t EC, uint16_t ER) {
    
	/* As RM690B0 driver need offset (see ORIENTATIONS_GENERAL) then the memory area needs to follow offsets*/
	SC += self->col_start;
	SR += self->row_start;
	EC += self->col_start;
	ER += self->row_start;

	uint8_t bufx[4] = {	(SC >> 8), (SC & 0xFF),	(EC >> 8), (EC & 0xFF)};
	uint8_t bufy[4] = {	(SR >> 8), (SR & 0xFF), (ER >> 8), (ER & 0xFF)};
	//uint8_t bufz[1] = { 0x00 };
	
	write_spi(self, LCD_CMD_CASET, bufx, 4); //Write CASET
	write_spi(self, LCD_CMD_RASET, bufy, 4); //Write RASET
	//write_spi(self, LCD_CMD_RAMWR, bufz, 0); //Write empty in case no write is done just after
}


static void set_rotation(amoled_AMOLED_obj_t *self, uint8_t rotation) {

	// WRITE MADCTL VALUES
    self->madctl_val &= 0x1F; // keep ML, BGR, MH, RSMX and RSMY, but reset MY,MX, MV
    self->madctl_val |= self->rotations[rotation].madctl;
    write_spi(self, LCD_CMD_MADCTL, (uint8_t[]) { self->madctl_val }, 1);

    self->width = self->rotations[rotation].width;
    self->height = self->rotations[rotation].height;
	self->col_start = self->rotations[rotation].colstart;
	self->row_start = self->rotations[rotation].rowstart;

	set_area(self, 0, 0, self->width - 1, self->height - 1);
}

/*----------------------------------------------------------------------------------------------------
Below are initialization related functions.
-----------------------------------------------------------------------------------------------------*/


static mp_obj_t amoled_AMOLED_reset(mp_obj_t self_in) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (self->reset != MP_OBJ_NULL) {
        mp_hal_pin_obj_t reset_pin = mp_hal_get_pin_obj(self->reset);
        mp_hal_pin_write(reset_pin, self->reset_level);
        mp_hal_delay_ms(300);    
        mp_hal_pin_write(reset_pin, !self->reset_level);
        mp_hal_delay_ms(200);    
    } else {
        write_spi(self, LCD_CMD_SWRESET, NULL, 0);
    }

    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_1(amoled_AMOLED_reset_obj, amoled_AMOLED_reset);


//Init function for RM67162, RM690B0 and SH8601
static mp_obj_t amoled_AMOLED_init(mp_obj_t self_in) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(self_in);

	//Setup Common
	write_spi(self, LCD_CMD_SLPOUT, NULL, 0);  // SLEEP OUT
	mp_hal_delay_ms(120);
	
	//Setup Specific
	switch (self->type) {
        case 0:	//RM67162
			//Hardware setup
			write_spi(self, LCD_CMD_SWITCHMODE, (uint8_t[]) {0x05}, 1);      // 0x05 SWITCH TO MANUFACTURING PAGE 4 COMMAND
			write_spi(self, LCD_FAC_OVSSCONTROL, (uint8_t[]) {0x05}, 1);     // OVSS control set elvss -3.95v
			write_spi(self, LCD_CMD_SWITCHMODE, (uint8_t[]) {0x01}, 1);      // 0x05 SWITCH TO MANUFACTURING PAGE 1 COMMAND
			write_spi(self, LCD_FAC_OVSSVOLTAGE, (uint8_t[]) {0x25}, 1);     // SET OVSS voltage level.= -4.0V
			//Back to Normal setup
			write_spi(self, LCD_CMD_SWITCHMODE, (uint8_t[]) {0x00}, 1);      // 0x00 SWITCH TO USER COMMAND
			//Add SETSPIMODE ? SETDISPMODE ?
			write_spi(self, LCD_CMD_SETTSCANL, (uint8_t[]) {0x02, 0x58}, 2); // SET TEAR SCANLINE TO N = 0x0258 = 600
        break;
        case 1: //RM690B0
			//Hardware setup
			write_spi(self, LCD_CMD_SWITCHMODE, (uint8_t[]) {0x20}, 1);      // 0x20 SWITCH TO MANUFACTURING PANEL COMMAND
			write_spi(self, LCD_FAC_MIPI,(uint8_t[]) {0x0A}, 1);             // MIPI OFF
			write_spi(self, LCD_FAC_SPI,(uint8_t[]) {0x80}, 1);              // SPI Write ram
			write_spi(self, LCD_FAC_SWIRE1,(uint8_t[]) {0x51}, 1);           // ! 230918:SWIRE FOR BV6804
			write_spi(self, LCD_FAC_SWIRE2,(uint8_t[]) {0x2E}, 1);           // ! 230918:SWIRE FOR BV6804
			//Back to Normal setup
			write_spi(self, LCD_CMD_SWITCHMODE, (uint8_t[]) {0x00}, 1);      // 0x00 SWITCH TO USER COMMAND
			//write_spi(self, LCD_CMD_SETSPIMODE, (uint8_t[]) {0xA1}, 1);    // 0xA1 = 1010 0001, first bit = SPI interface write RAM enable
			write_spi(self, LCD_CMD_SETDISPMODE, (uint8_t[]) {0x00}, 1);     // Set DSI Mode to 0x00 = Internal Timmings
			mp_hal_delay_ms(10);
			write_spi(self, LCD_CMD_SETTSCANL, (uint8_t[]) {0x02, 0x18}, 2); // SET TEAR SCANLINE TO N = 0x218 = 536
		 break;
		 case 2: //SH8601
			//Hardware setup
			write_spi(self, LCD_CMD_SWITCHMODE, (uint8_t[]) {0x20}, 1);      //Switch to Cde HBM mode
			write_spi(self, 0x63, (uint8_t[]) {0xFF}, 1);                    // ? Write brightness HBM
			write_spi(self, 0x26, (uint8_t[]) {0x0A}, 1);                    // ?
			write_spi(self, 0x24, (uint8_t[]) {0x80}, 1);                    // ? 
			//Back to Normal setup
			write_spi(self, LCD_CMD_SWITCHMODE, (uint8_t[]) {0x20}, 1);      //Turn back to Cde monde		
			write_spi(self, LCD_CMD_SETSPIMODE, (uint8_t[]) { 0x80 }, 1);    // QSPI MODE
			write_spi(self, LCD_CMD_SETDISPMODE, (uint8_t[]) { 0x00 }, 1);   // DSPI MODE OFF
			mp_hal_delay_ms(10);
			write_spi(self, LCD_CMD_WRCTRLD1, (uint8_t[]) {0x20}, 1);        // Set Brightness control ON to Display 1
			write_spi(self, LCD_CMD_SETTSCANL, (uint8_t[]) {0x01, 0xC0}, 2); // SET TEAR SCANLINE TO N = 0x01C0 = 448
		break;
	}
		
	//Setup Common Final
	
	//Finish and Enlight display
	write_spi(self, LCD_CMD_COLMOD, (uint8_t[]) { self->colmod_cal }, 1);    // Interface Pixel Format 0x55 16bpp x66 18bpp 0x77 24bpp
	write_spi(self, LCD_CMD_WRDISBV, (uint8_t[]) {0x00}, 1);                 // WRITE BRIGHTNESS MIN VALUE 0x00
	write_spi(self, LCD_CMD_TEON, (uint8_t[]) {0x00}, 1);                    // TEAR OFF
	write_spi(self, LCD_CMD_DISPON, NULL, 0);                                // DISPLAY ON
	mp_hal_delay_ms(10);
	
	//Fill display with the framebuffer previously initialized
	write_color(self, self->fram_buf, self->width * self->height * self->Bpp);
	
	//Finillay set brighness	
	write_spi(self, LCD_CMD_WRDISBV, (uint8_t[]) {0xFF}, 1);                // WRITE BRIGHTNESS MAX VALUE 0xFF
	
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_1(amoled_AMOLED_init_obj, amoled_AMOLED_init);


mp_obj_t amoled_AMOLED_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    enum {
        ARG_bus,
		ARG_type,
        ARG_reset,
        ARG_reset_level,
        ARG_color_space,
        ARG_bpp,
		ARG_rotation,
        ARG_auto_refresh,
		ARG_bus_methode,   //FOR DEVELOPPEMENT PURPOSE
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_bus,              MP_ARG_OBJ | MP_ARG_REQUIRED, {.u_obj = MP_OBJ_NULL}     },
	    { MP_QSTR_type,             MP_ARG_INT | MP_ARG_KW_ONLY,  {.u_int = 1}    			 },
        { MP_QSTR_reset,            MP_ARG_OBJ | MP_ARG_KW_ONLY,  {.u_obj = MP_OBJ_NULL}     },
        { MP_QSTR_reset_level,      MP_ARG_BOOL | MP_ARG_KW_ONLY, {.u_bool = false}          },
        { MP_QSTR_color_space,      MP_ARG_INT | MP_ARG_KW_ONLY,  {.u_int = COLOR_SPACE_RGB} },
        { MP_QSTR_bpp,              MP_ARG_INT | MP_ARG_KW_ONLY,  {.u_int = 16}              },
		{ MP_QSTR_rotation,         MP_ARG_INT | MP_ARG_KW_ONLY,  {.u_int = 0}               },
		{ MP_QSTR_auto_refresh,		MP_ARG_INT | MP_ARG_KW_ONLY,  {.u_bool = true}           },
		{ MP_QSTR_bus_methode,      MP_ARG_INT | MP_ARG_KW_ONLY,  {.u_int = 0}               }, //FOR DEVELOPPEMENT PURPOSE
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(
        n_args,
        n_kw,
        all_args,
        MP_ARRAY_SIZE(allowed_args),
        allowed_args,
        args
    );

    // create new object
    amoled_AMOLED_obj_t *self = m_new_obj(amoled_AMOLED_obj_t);
    self->base.type = &amoled_AMOLED_type;

    self->bus_obj = (mp_obj_base_t *)MP_OBJ_TO_PTR(args[ARG_bus].u_obj);
#ifdef MP_OBJ_TYPE_GET_SLOT
    self->lcd_panel_p = (amoled_panel_p_t *)MP_OBJ_TYPE_GET_SLOT(self->bus_obj->type, protocol);
#else
    self->lcd_panel_p = (amoled_panel_p_t *)self->bus_obj->type->protocol;
#endif

	//Display type 0 = TDisplay S3 RM61672 / 1 = T4-S3 RM690B0 / 2 = WAVESHARE SH8601
	self->type = args[ARG_type].u_int;

	//Get other arguments
	self->auto_refresh = args[ARG_auto_refresh].u_bool;
    self->reset        = args[ARG_reset].u_obj;
    self->reset_level  = args[ARG_reset_level].u_bool;
    self->color_space  = args[ARG_color_space].u_int;
    self->bpp          = args[ARG_bpp].u_int;
	self->Bpp		   = (self->bpp + 6) >> 3;  // 16 : 2 / 18 : 3 / 24 : 3
	self->rotation     = args[ARG_rotation].u_int;
	self->madctl_val   = 0;
	self->bus_methode  = args[ARG_bus_methode].u_int;   //FOR DEVELOPPEMENT PURPOSE
	
	// set RGB or BGR
    switch (self->color_space) {
        case COLOR_SPACE_RGB:
            self->madctl_val &= ~MADCTL_BGR_BIT;  //Set Color bit to 0
        break;

        case COLOR_SPACE_BGR:
            self->madctl_val |= MADCTL_BGR_BIT; //Set Color bit to 1
        break;

        default:
            mp_raise_ValueError(MP_ERROR_TEXT("unsupported color space"));
        break;
    }

	// Select COLMOD calibration and Filter depending on bpp
	uint8_t colmod_cal = 0x0;
	uint8_t colmod_fil = 0x0;
	
    switch (self->bpp) {
        case 16:
            colmod_cal = COLMOD_CAL_16;
			colmod_fil = COLMOD_FIL_16;
			//Get bpp filter and switches
        break;

        case 18:
            colmod_cal = COLMOD_CAL_18;
			colmod_fil = COLMOD_FIL_18;
        break;

        case 24:
			colmod_cal = COLMOD_CAL_24;
			colmod_fil = COLMOD_FIL_24;
        break;

        default:
            mp_raise_ValueError(MP_ERROR_TEXT("unsupported pixel width"));
        break;
    }
	
	// And apply them
	self->colmod_cal = colmod_cal;
	self->bpp_process.fltr_col_rd = 	BPP_PROCESS_GEN[colmod_fil].fltr_col_rd;
	self->bpp_process.bitsw_col_rd = 	BPP_PROCESS_GEN[colmod_fil].bitsw_col_rd;
	self->bpp_process.fltr_col_gr = 	BPP_PROCESS_GEN[colmod_fil].fltr_col_gr;
	self->bpp_process.bitsw_col_gr = 	BPP_PROCESS_GEN[colmod_fil].bitsw_col_gr;
	self->bpp_process.fltr_col_bl = 	BPP_PROCESS_GEN[colmod_fil].fltr_col_bl;

	//Get rotation infos depending of display type
    bzero(&self->rotations, sizeof(self->rotations));
	switch (self->type) {
        case 0:
            memcpy(&self->rotations, ORIENTATIONS_RM67162, sizeof(ORIENTATIONS_RM67162));
        break;
        case 1:
            memcpy(&self->rotations, ORIENTATIONS_RM690B0, sizeof(ORIENTATIONS_RM690B0));
        break;
		case 2:
            memcpy(&self->rotations, ORIENTATIONS_SH8601, sizeof(ORIENTATIONS_SH8601));
        break;			
		default:
            mp_raise_ValueError(MP_ERROR_TEXT("Unsupported display type"));
        break;
	}

	 //Reset the chip
	amoled_AMOLED_reset(self);
	
	//Setup display rotation and get display parameters
	set_rotation(self, self->rotation);
	
	//Allocate a corresponding frame buffer
    self->fram_buf = heap_caps_aligned_calloc(RAM_ALIGNMENT, self->width * self->height, self->Bpp, MALLOC_CAP_8BIT |MALLOC_CAP_SPIRAM); 
	
    if (self->fram_buf == NULL) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("Failed to allocate Frame Buffer."));
    }

	//Finally initialize the display
	amoled_AMOLED_init(self);
	
    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t amoled_AMOLED_deinit(mp_obj_t self_in) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (self->lcd_panel_p) {
        self->lcd_panel_p->deinit(self->bus_obj);
    }

    heap_caps_free((void*)self->fram_buf);
	self->fram_buf = NULL;

    //m_del_obj(amoled_AMOLED_obj_t, self); 
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_1(amoled_AMOLED_deinit_obj, amoled_AMOLED_deinit);


static mp_obj_t amoled_TTF_deinit(mp_obj_t self_in) {
    SFT *self = (SFT *)MP_OBJ_TO_PTR(self_in);

    heap_caps_free((void *)self->font->memory);
	self->font->memory = NULL;
	heap_caps_free((void *)self->font);
	self->font = NULL;

    //m_del_obj(amoled_TTF_obj_t, self); 
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_1(amoled_TTF_deinit_obj, amoled_TTF_deinit);


/*----------------------------------------------------------------------------------------------------
Below are library information related functions.
-----------------------------------------------------------------------------------------------------*/


//Print display informations
static void amoled_AMOLED_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t  kind) {
    (void) kind;
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(
        print,
        "<AMOLED Display - Bus=%p, Reset=%p, Color_space=%s, Bpp=%u>",
        self->bus_obj,
        self->reset,
        color_space_desc[self->color_space],
        self->bpp
    );
}

//Print Font informations
static void amoled_TTF_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t  kind) {
    (void) kind;
    SFT *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(
        print,
        "<AMOLED TTF - Scale X=%.1f Y=%.1f, Offset X=%.0f Y=%.0f, Kerning=%u, Flags=%u>",
        self->xScale,
        self->yScale,
		self->xOffset,
		self->yOffset,
		self->kerning,
		self->flags
    );
}

static mp_obj_t amoled_AMOLED_version() {   
    return mp_obj_new_str(AMOLED_DRIVER_VERSION, 10);
}

static MP_DEFINE_CONST_FUN_OBJ_0(amoled_AMOLED_version_obj, amoled_AMOLED_version);


/*-----------------------------------------------------------------------------------------------------
Below are buffers and screen buffers related function.
------------------------------------------------------------------------------------------------------*/


//Return color from R,G,B values
static uint16_t colorRGB(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3);
    return ((((c) >> 8) & 0x00FF) | (((c) << 8) & 0xFF00));
}

static mp_obj_t amoled_AMOLED_colorRGB(size_t n_args, const mp_obj_t *args) {
    return MP_OBJ_NEW_SMALL_INT(colorRGB(
        (uint8_t)mp_obj_get_int(args[1]),
        (uint8_t)mp_obj_get_int(args[2]),
        (uint8_t)mp_obj_get_int(args[3])));
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_colorRGB_obj, 4, 4, amoled_AMOLED_colorRGB);


//This function send a part of the frame_buffer to the display memory
static void refresh_display(amoled_AMOLED_obj_t *self, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {

	if (self->auto_refresh) {
		
		uint8_t  BPP=self->Bpp;
		uint16_t WIDTH=self->width;
		
		// The SC[15:0] and SR[15:0] must be divisible by 2 (SC or SR Even)
		uint16_t SC = (x >> 1) << 1;
		uint16_t SR = (y >> 1) << 1;

		//EC[15:0]-SC[15:0]+1 must be divisible by 2 (so EC must be Odd)
		uint16_t EC = (((x+w-1) >> 1) << 1 ) + 1;
		uint16_t ER = (((y+h-1) >> 1) << 1 ) + 1;

		//calculate real width
		uint16_t w1 = EC - SC + 1;
		uint16_t h1 = ER - SR + 1;
		
		uint32_t temp_buf_size = 0;
		uint32_t temp_buf_write_size = 0;
		
		if(self->bus_methode == 0) {
			
			//Simpliest version direct writing (write to display every 2 lines)
			
			temp_buf_size = 2 * w1;            //2 lines of given width
			uint16_t temp_buf[temp_buf_size];  //Allocate an array
			size_t  fram_buf_idx = 0;
			size_t  temp_buf_idx = 0;
			temp_buf_write_size = temp_buf_size*BPP;
			bool b = false;
			
			
			for(uint16_t line = SR; line <= ER; line++) {
				fram_buf_idx = line * WIDTH + SC;
				for(uint16_t col = SC; col <= EC; col++) {
					temp_buf[temp_buf_idx]=self->fram_buf[fram_buf_idx];
					fram_buf_idx++;
					temp_buf_idx++;
				}
				if (b) { //One every two line we write to the display
					set_area(self, SC, line-1, EC, line);
					write_color(self, temp_buf, temp_buf_write_size);
					temp_buf_idx = 0;
					b = false;
				} else {
					b = true;
				}
			}
		}
		
		if(self->bus_methode == 1) {
		
			//Original code leading to artefact, I believe they where due to the allocation
			//of buffer in SPIRAM that was conflicting with SPI transmission. The code was damn fast
			//but artefacts where unacceptables... 
			//Line with MALLOC_CAP_DMA doesn't change but crashes with too big buffer (lack of memoy)
			//Alignment doesn't seem to change anything (maybe faster...writing but still artefacts)
			
			temp_buf_size = w1 * h1;   // full buffered
			self->temp_buf = heap_caps_aligned_calloc(RAM_ALIGNMENT, temp_buf_size, BPP, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
			
			//Copy frame_buffer to partial_frame_buffer
			size_t  fram_buf_idx = 0;
			size_t  temp_buf_idx = 0;
			
			temp_buf_write_size = w1*BPP;

			for (uint16_t line = SR; line <= ER; line++) {
				fram_buf_idx = line * WIDTH + SC;
				memcpy(&self->temp_buf[temp_buf_idx], &self->fram_buf[fram_buf_idx], temp_buf_write_size);
				temp_buf_idx += w1;
			}
			
			//Set display areas and write the partial frame buffer
			set_area(self, SC, SR, EC, ER);
			write_color(self, self->temp_buf, temp_buf_size * BPP);

			//Than partial frame buffer  memory and return
			heap_caps_free(self->temp_buf);
		}
		
		if(self->bus_methode == 2) {
		
			//Hybrid method, copy to temp_fb without memcpy and write the full buffer to display
			
			temp_buf_size = w1 * h1;   // full buffered
			self->temp_buf = heap_caps_aligned_calloc(RAM_ALIGNMENT, temp_buf_size, BPP, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
			
			//Copy frame_buffer to partial_frame_buffer
			size_t  fram_buf_idx = 0;
			size_t  temp_buf_idx = 0;
			
			for(uint16_t line = SR; line <= ER; line++) {
				fram_buf_idx = line * WIDTH + SC;
				for(uint16_t col = SC; col <= EC; col++) {
					self->temp_buf[temp_buf_idx]=self->fram_buf[fram_buf_idx];
					fram_buf_idx++;
					temp_buf_idx++;
				}
			}
			
			//Set display areas and write the partial frame buffer
			set_area(self, SC, SR, EC, ER);
			write_color(self, self->temp_buf, temp_buf_size * BPP);

			//Than partial frame buffer  memory and return
			heap_caps_free(self->temp_buf);
		}
	}
}

//Refresh whole (if no args) or a portion of the display (need x,y,w,h)
static mp_obj_t amoled_AMOLED_refresh(size_t n_args, const mp_obj_t *args) {
	amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
	bool save_auto_refresh = self->auto_refresh;  //Save auto_refresh value
		
	self->auto_refresh = true;	// Allow to write to screen buffer
	
	if (n_args > 4) {	//if x0..y1 exist, only update partial area
		refresh_display(self, mp_obj_get_int(args[1]), mp_obj_get_int(args[2]), mp_obj_get_int(args[3]),  mp_obj_get_int(args[4]));
	} else {			//otherwise update full screen
		refresh_display(self, 0, 0, self->width, self->height);
	}
	self->auto_refresh = save_auto_refresh;  //Restore auto_refresh value
	
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_refresh_obj, 1, 5, amoled_AMOLED_refresh);


// This fill the frame buffer area, it has no dimension check, all should be done previously
static void fill_frame_buffer(amoled_AMOLED_obj_t *self, uint16_t color, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
	
	size_t fram_buf_idx;
	
    if (self->fram_buf == NULL) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("No framebuffer available."));
    }
	
	for (uint16_t line = 0; line < h; line++) {
		fram_buf_idx = ((y + line) * self->width) + x;
		wmemset(&self->fram_buf[fram_buf_idx],color, w);
	}

    if ((!self->hold_display) & (self->auto_refresh)) {
		refresh_display(self,x,y,w,h);
	}
}


/*-----------------------------------------------------------------------------------------------------
Below are drawing functions : Pixel, lines, rectangles, filled circles, a.s.o
------------------------------------------------------------------------------------------------------*/


static void pixel(amoled_AMOLED_obj_t *self, uint16_t x, uint16_t y, uint16_t color) {
	uint32_t fram_buf_idx;
	if ((x < self->width) & (y < self->height)) {
		fram_buf_idx = (y * self->width) + x;
		self->fram_buf[fram_buf_idx] = color;
		if (!self->hold_display & self->auto_refresh) {
			refresh_display(self,x,y,1,1);
		}
	}
}

static mp_obj_t amoled_AMOLED_pixel(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint16_t x = mp_obj_get_int(args[1]);
    uint16_t y = mp_obj_get_int(args[2]);
    uint16_t color = mp_obj_get_int(args[3]);

    pixel(self, x, y, color);
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_pixel_obj, 4, 4, amoled_AMOLED_pixel);


static mp_obj_t amoled_AMOLED_fill(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint16_t color = mp_obj_get_int(args[1]);
	
    fill_frame_buffer(self, color, 0, 0, self->width, self->height);
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_fill_obj, 2, 2, amoled_AMOLED_fill);


static void fast_hline(amoled_AMOLED_obj_t *self, uint16_t x, uint16_t y, uint16_t len, uint16_t color) {
	if ((x < self->width) & (y < self->height) & (len >0)){
		if (x + len > self->width) {
			len = self->width - x;
		} 
		fill_frame_buffer(self, color, x, y, len, 1);
	}
}

static mp_obj_t amoled_AMOLED_hline(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint16_t x = mp_obj_get_int(args[1]);
    uint16_t y = mp_obj_get_int(args[2]);
    uint16_t len = mp_obj_get_int(args[3]);
    uint16_t color = mp_obj_get_int(args[4]);

    fast_hline(self, x, y, len, color);
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_hline_obj, 5, 5, amoled_AMOLED_hline);


static void fast_vline(amoled_AMOLED_obj_t *self, uint16_t x, uint16_t y, uint16_t len, uint16_t color) {
	if ((x < self->width) & (y < self->height) & (len >0)){
		if (y + len > self->height) {
			len = self->height - y;
		} 
		fill_frame_buffer(self, color, x, y, 1, len);
	}
}

static mp_obj_t amoled_AMOLED_vline(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint16_t x = mp_obj_get_int(args[1]);
    uint16_t y = mp_obj_get_int(args[2]);
    uint16_t len = mp_obj_get_int(args[3]);
    uint16_t color = mp_obj_get_int(args[4]);

    fast_vline(self, x, y, len, color);
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_vline_obj, 5, 5, amoled_AMOLED_vline);


static void line(amoled_AMOLED_obj_t *self, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color) {
    bool steep = ABS(y1 - y0) > ABS(x1 - x0);
	bool saved_hold_display = self->hold_display;
	
    if (steep) {
        SWAP16(x0, y0);
        SWAP16(x1, y1);
    }

    if (x0 > x1) {
        SWAP16(x0, x1);
        SWAP16(y0, y1);
    }

    int16_t dx = x1 - x0, dy = ABS(y1 - y0);
    int16_t err = dx >> 1, ystep = -1, xs = x0, dlen = 0;

    if (y0 < y1) {
        ystep = 1;
    }
	
	self->hold_display = true;
	
    // Split into steep and not steep for FastH/V separation
    if (steep) {
        for (; x0 <= x1; x0++) {
            dlen++;
            err -= dy;
            if (err < 0) {
                err += dx;
                fast_vline(self, y0, xs, dlen, color);
                dlen = 0;
                y0 += ystep;
                xs = x0 + 1;
            }
        }
        if (dlen) {
            fast_vline(self, y0, xs, dlen, color);
        }
    } else {
        for (; x0 <= x1; x0++) {
            dlen++;
            err -= dy;
            if (err < 0) {
                err += dx;
                fast_hline(self, xs, y0, dlen, color);
                dlen = 0;
                y0 += ystep;
                xs = x0 + 1;
            }
        }
        if (dlen) {
            fast_hline(self, xs, y0, dlen, color);
        }
    }
	// Restore hold_display status
	self->hold_display = saved_hold_display;
	if (!self->hold_display & self->auto_refresh) {
		refresh_display(self,x0,y0,x1-x0,(y1>y0)?(y1-y0):(y0-y1));
	}
}

static mp_obj_t amoled_AMOLED_line(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint16_t x0 = mp_obj_get_int(args[1]);
    uint16_t y0 = mp_obj_get_int(args[2]);
    uint16_t x1 = mp_obj_get_int(args[3]);
    uint16_t y1 = mp_obj_get_int(args[4]);
    uint16_t color = mp_obj_get_int(args[5]);

    line(self, x0, y0, x1, y1, color);
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_line_obj, 6, 6, amoled_AMOLED_line);


static void rect(amoled_AMOLED_obj_t *self, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {

	if (x + w > self->width || y + h > self->height || w == 0 || h ==0) {
		return;
	} 
	if (h == 1){
		fast_hline(self, x, y, w, color);
		return;
	}
	if (w == 1){
		fast_vline(self, x, y, h, color);
		return;
	}
	self->hold_display = true;
    fast_hline(self, x, y, w, color);
    fast_hline(self, x, y + h - 1, w, color);
    fast_vline(self, x, y, h, color);
    fast_vline(self, x + w - 1, y, h, color);
	self->hold_display = false;
	refresh_display(self,x,y,w,h);
}

static mp_obj_t amoled_AMOLED_rect(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint16_t x = mp_obj_get_int(args[1]);
    uint16_t y = mp_obj_get_int(args[2]);
    uint16_t w = mp_obj_get_int(args[3]);
    uint16_t h = mp_obj_get_int(args[4]);
    uint16_t color = mp_obj_get_int(args[5]);

    rect(self, x, y, w, h, color);
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_rect_obj, 6, 6, amoled_AMOLED_rect);


static void fill_rect(amoled_AMOLED_obj_t *self, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {

	/* Check are done to see if COL and ROW START ARE EVEN and COL and ROW END minus START are also divisible by 2*/
	if (x + w > self->width || y + h > self->height || w == 0 || h ==0) {
		return;
	}
	fill_frame_buffer(self, color, x, y, w, h);
}

static mp_obj_t amoled_AMOLED_fill_rect(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint16_t x = mp_obj_get_int(args[1]);
    uint16_t y = mp_obj_get_int(args[2]);
    uint16_t w = mp_obj_get_int(args[3]);
    uint16_t l = mp_obj_get_int(args[4]);
    uint16_t color = mp_obj_get_int(args[5]);

    fill_rect(self, x, y, w, l, color);
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_fill_rect_obj, 6, 6, amoled_AMOLED_fill_rect);


static void trian(amoled_AMOLED_obj_t *self, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color) {

	uint16_t xmin = min_val(min_val(x0,x1),x2);
	uint16_t xmax = max_val(max_val(x0,x1),x2);
	uint16_t ymin = min_val(min_val(y0,y1),y2);
	uint16_t ymax = max_val(max_val(y0,y1),y2);

	self->hold_display = true;
	line(self, x0, y0, x1, y1, color);
	line(self, x1, y1, x2, y2, color);
	line(self, x0, y0, x2, y2, color);
	self->hold_display = false;
	refresh_display(self,xmin,ymin,xmax-xmin,ymax-ymin);
}

static mp_obj_t amoled_AMOLED_trian(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint16_t x0 = mp_obj_get_int(args[1]);
    uint16_t y0 = mp_obj_get_int(args[2]);
	uint16_t x1 = mp_obj_get_int(args[3]);
    uint16_t y1 = mp_obj_get_int(args[4]);
	uint16_t x2 = mp_obj_get_int(args[5]);
    uint16_t y2 = mp_obj_get_int(args[6]);
    uint16_t color = mp_obj_get_int(args[7]);

    trian(self, x0, y0, x1, y1, x2, y2, color);
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_trian_obj, 8, 8, amoled_AMOLED_trian);


static void fill_trian(amoled_AMOLED_obj_t *self, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color) {

	uint16_t xmin = min_val(min_val(x0,x1),x2);
	uint16_t xmax = max_val(max_val(x0,x1),x2);
	mp_float_t dx02;
	mp_float_t dx01;
	mp_float_t dx12;
	mp_float_t x01;
	mp_float_t x02;
	mp_float_t x12;
	
	//Sort corners by y value (y0 < y1 < y2)
	if (y1 < y0) {
		SWAP16(x0, x1);
        SWAP16(y0, y1);
	}
	if (y2 < y0) {
		SWAP16(x0, x2);
        SWAP16(y0, y2);
	}	
	if (y2 < y1) {
		SWAP16(x1, x2);
        SWAP16(y1, y2);
	}		
	
	if (y2 == y0) {
		fast_hline(self,xmin,y0,xmax-xmin,color);
		return;
	}

	self->hold_display = true;
	
	dx02 = (float)(x2 - x0) / (float)(y2 - y0);
	x02 = x0;
	x01 = x0;
	//Process lower sub-triangle
	//Check if triangle has flat bottom
	if (y1 > y0) {
		dx01 = (float)(x1 - x0) / (float)(y1 - y0);
		for(uint16_t y=y0; y<=y1; y++) {
			if (x01 <= x02) {
				fast_hline(self,(int)x01,y,(int)(x02-x01),color);
			} else {
				fast_hline(self,(int)x02,y,(int)(x01-x02),color);
			}
			x02 += dx02;
			x01 += dx01;
		}
	}
	//Process Upper sub-triangle
	/*Check if triangle has flat top*/
	if (y2 > y1) {
		dx12 = (float)(x2 - x1) / (float)(y2 - y1);
		x12 = x1 + dx12; //we alreardy proceed up to y1 so 
		for(uint16_t y=y1+1; y<=y2; y++) {
			if (x02 <= x12) {
				fast_hline(self,(int)x02,y,(int)(x12-x02),color);
			} else {
				fast_hline(self,(int)x12,y,(int)(x02-x12),color);
			}
			x02 += dx02;
			x12 += dx12;		
		}
	}
	self->hold_display = false;
	refresh_display(self,xmin,y0,xmax-xmin,y2-y0);
}

static mp_obj_t amoled_AMOLED_fill_trian(size_t n_args, const mp_obj_t *args) {
	amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
	uint16_t x0 = mp_obj_get_int(args[1]);
	uint16_t y0 = mp_obj_get_int(args[2]);
	uint16_t x1 = mp_obj_get_int(args[3]);
	uint16_t y1 = mp_obj_get_int(args[4]);
	uint16_t x2 = mp_obj_get_int(args[5]);
	uint16_t y2 = mp_obj_get_int(args[6]);
	uint16_t color = mp_obj_get_int(args[7]);
	fill_trian(self, x0, y0, x1, y1, x2, y2, color);
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_fill_trian_obj, 8, 8, amoled_AMOLED_fill_trian);


static void bubble_rect(amoled_AMOLED_obj_t *self, uint16_t xs, uint16_t ys, uint16_t w, uint16_t h, uint16_t color) {
    if (xs + w > self->width || ys + h > self->height) {
        return;
    }

    int bubble_size = min_val(w, h) / 4; 
    int xm = xs + bubble_size;
    int ym = ys + bubble_size;
    
	self->hold_display = true;
	
	//Process sides

	fast_hline(self, xm, ys, w - bubble_size * 2, color);
	fast_hline(self, xm, ys + h - 1, w - bubble_size * 2, color);
	fast_vline(self, xs, ym , h - bubble_size * 2, color);
    fast_vline(self, xs + w -1, ym , h - bubble_size * 2, color);

	//Process angles
	if(bubble_size > 1) {
		/*int x = 0;
		int y = bubble_size;
		int p = 1 - bubble_size;  p is always <0 at beginning so avoid processins x=0*/
		int x = 1;
		int y = bubble_size;
		int p = 6 - bubble_size;  // p=(1 - bubble_size) +2x + 3 = 6-bubble_size
		
		while (x <= y){
			// top left
			pixel(self, xm - x, ym - y, color);
			pixel(self, xm - y, ym - x, color);
			
			// top right
			pixel(self, xm + w - bubble_size * 2 + x - 1, ym - y, color);
			pixel(self, xm + w - bubble_size * 2 + y - 1, ym - x, color);
			
			// bottom left
			pixel(self, xm - x, ym + h - bubble_size * 2 + y - 1, color);
			pixel(self, xm - y, ym + h - bubble_size * 2 + x - 1, color);
			
			// bottom right
			pixel(self, xm + w - bubble_size * 2 + x - 1, ym + h - bubble_size * 2 + y - 1, color);
			pixel(self, xm + w - bubble_size * 2 + y - 1, ym + h - bubble_size * 2 + x - 1, color);
			
			if (p < 0) {
				p += 2 * x + 3;
			} else {
				p += 2 * (x - y) + 5;
				y -= 1;
			}
			x += 1;
		}
	}
	
	self->hold_display = false;
	refresh_display(self,xs,ys,w,h);
}

static mp_obj_t amoled_AMOLED_bubble_rect(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint16_t x = mp_obj_get_int(args[1]);
    uint16_t y = mp_obj_get_int(args[2]);
    uint16_t w = mp_obj_get_int(args[3]);
    uint16_t h = mp_obj_get_int(args[4]);
    uint16_t color = mp_obj_get_int(args[5]);

    bubble_rect(self, x, y, w, h, color);
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_bubble_rect_obj, 6, 6, amoled_AMOLED_bubble_rect);


static void fill_bubble_rect(amoled_AMOLED_obj_t *self, uint16_t xs, uint16_t ys, uint16_t w, uint16_t h, uint16_t color) {
    if (xs + w > self->width || ys + h > self->height) {
        return;
    }
    int bubble_size = min_val(w, h) / 4; 
    int xm = xs + bubble_size;
    int ym = ys + bubble_size;
    
	self->hold_display = true;
	
	//Process internal rectangle
    fill_rect(self, xs, ym, w, h - bubble_size * 2, color);

	//Process upper and lower rounded rectangles
	if(bubble_size >= 1) {
		/*int x = 0;
		int y = bubble_size;
		int p = 1 - bubble_size;  p is always <0 at beginning so avoid processins x=0*/
		int x = 1;
		int y = bubble_size;
		int p = 6 - bubble_size;  // p=(1 - bubble_size) +2x + 3 = 6-bubble_size


		while (x <= y) {
			// top left to right
			fast_hline(self, xm - x, ym - y, w - bubble_size * 2 + x * 2 - 1, color);
			fast_hline(self, xm - y, ym - x, w - bubble_size * 2 + y * 2 - 1, color);
			
			// bottom left to right
			fast_hline(self, xm - x, ym + h - bubble_size * 2 + y - 1, w - bubble_size * 2 + x * 2 - 1, color);
			fast_hline(self, xm - y, ym + h - bubble_size * 2 + x - 1, w - bubble_size * 2 + y * 2 - 1, color);
			
			if (p < 0) {
				p += 2 * x + 3;
			} else {
				p += 2 * (x - y) + 5;
				y -= 1;
			} 
			x += 1;
		}
	}
	self->hold_display = false;
	refresh_display(self,xs,ys,w,h);
}

static mp_obj_t amoled_AMOLED_fill_bubble_rect(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint16_t x = mp_obj_get_int(args[1]);
    uint16_t y = mp_obj_get_int(args[2]);
    uint16_t w = mp_obj_get_int(args[3]);
    uint16_t h = mp_obj_get_int(args[4]);
    uint16_t color = mp_obj_get_int(args[5]);

    fill_bubble_rect(self, x, y, w, h, color);
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_fill_bubble_rect_obj, 6, 6, amoled_AMOLED_fill_bubble_rect);


static void circle(amoled_AMOLED_obj_t *self, uint16_t xm, uint16_t ym, uint16_t r, uint16_t color) {
    
	self->hold_display = true;
	
	if (r == 0){
		pixel(self, xm , ym , color);
	}
	else {
		int x = 0;
		int y = r;
		int p = 1 - r;	

		while (x <= y) {
			pixel(self, xm + x, ym + y, color);
			pixel(self, xm + x, ym - y, color);
			pixel(self, xm - x, ym + y, color);
			pixel(self, xm - x, ym - y, color);
			pixel(self, xm + y, ym + x, color);
			pixel(self, xm + y, ym - x, color);
			pixel(self, xm - y, ym + x, color);
			pixel(self, xm - y, ym - x, color);

			if (p < 0) {
				p += 2 * x + 3;
			} else {
				p += 2 * (x - y) + 5;
				y -= 1;
			}
			x += 1;
		}
	}
	
	self->hold_display = false;
	refresh_display(self,xm-r,ym-r,2*r+1,2*r+1);
}

static mp_obj_t amoled_AMOLED_circle(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint16_t xm = mp_obj_get_int(args[1]);
    uint16_t ym = mp_obj_get_int(args[2]);
    uint16_t r = mp_obj_get_int(args[3]);
    uint16_t color = mp_obj_get_int(args[4]);

    circle(self, xm, ym, r, color);
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_circle_obj, 5, 5, amoled_AMOLED_circle);

static void fill_circle(amoled_AMOLED_obj_t *self, uint16_t xm, uint16_t ym, uint16_t r, uint16_t color) {
	
 	self->hold_display = true;
	
	if (r == 0){
		pixel(self, xm , ym , color);
	}
	else {
		int x = 0;
		int y = r;
		int p = 1 - r;
		
		while (x <= y) {
			fast_vline(self, xm + x, ym - y, 2 * y, color);
			fast_vline(self, xm - x, ym - y, 2 * y, color);
			fast_vline(self, xm + y, ym - x, 2 * x, color);
			fast_vline(self, xm - y, ym - x, 2 * x, color);

			if (p < 0) {
				p += 2 * x + 3;
			} else {
				p += 2 * (x - y) + 5;
				y -= 1;
			}
			x += 1;
		}
	}
	
	self->hold_display = false;
	refresh_display(self,xm-r,ym-r,2*r+1,2*r+1);
}

static mp_obj_t amoled_AMOLED_fill_circle(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint16_t xm = mp_obj_get_int(args[1]);
    uint16_t ym = mp_obj_get_int(args[2]);
    uint16_t r = mp_obj_get_int(args[3]);
    uint16_t color = mp_obj_get_int(args[4]);

    fill_circle(self, xm, ym, r, color);
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_fill_circle_obj, 5, 5, amoled_AMOLED_fill_circle);


static void ellipse(amoled_AMOLED_obj_t *self, uint16_t xm, uint16_t ym, uint16_t rx, uint16_t ry, uint16_t color) {

	self->hold_display = true;

	//If ellipse is flat, général algorythm will fall into loop
	if ((rx == 0)|(ry == 0)){
		if (rx == 0) {
			fast_hline(self, xm - ry, ym, 2 * ry, color);
		}
		if (ry == 0){
			fast_vline(self, xm, ym-rx, 2 * rx, color);
		}
	}
	else {
		
		int x = 0;
		int y = ry;
	
		//let to check vs int
		float d1 = (ry * ry) - (rx * rx * ry) + (0.25 * rx * rx);
		float dx = 0;
		float dy = 2 * rx * rx * y;
		
		// Plotting points of region 1	
		while (dx <= dy) {
			pixel(self, xm + x, ym + y, color);
			pixel(self, xm + x, ym - y, color);
			pixel(self, xm - x, ym + y, color);
			pixel(self, xm - x, ym - y, color);

			if (d1 < 0) {
				x++;
				dx = dx + (2 * ry * ry);
				d1 = d1 + dx + (ry * ry);
			} 
			else {
				x++;
				y--;
				dx = dx + (2 * ry * ry);
				dy = dy - (2 * rx * rx);
				d1 = d1 + dx - dy + (ry * ry);
			}
		}

		int d2 = ((ry * ry) * ((x + 0.5) * (x + 0.5))) + ((rx * rx) * ((y - 1) * (y - 1))) - (rx * rx * ry * ry);

		// Plotting points of region 2
		while (y >= 0) {
			pixel(self, xm + x, ym + y, color);
			pixel(self, xm + x, ym - y, color);
			pixel(self, xm - x, ym + y, color);
			pixel(self, xm - x, ym - y, color);
			
			if (d2 > 0) {
				y--;
				dy = dy - (2 * rx * rx);
				d2 = d2 + (rx * rx) - dy;
			} 
			else {
				y--;
				x++;
				dx = dx + (2 * ry * ry);
				dy = dy - (2 * rx * rx);
				d2 = d2 + dx - dy + (rx * rx);
			}
		}
	}

	self->hold_display = false;
	refresh_display(self,xm-rx,ym-ry,2*rx+1,2*ry+1);
}

static mp_obj_t amoled_AMOLED_ellipse(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint16_t xm = mp_obj_get_int(args[1]);
    uint16_t ym = mp_obj_get_int(args[2]);
    uint16_t rx = mp_obj_get_int(args[3]);
	uint16_t ry = mp_obj_get_int(args[4]);
    uint16_t color = mp_obj_get_int(args[5]);

    ellipse(self, xm, ym, rx, ry, color);
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_ellipse_obj, 6, 6, amoled_AMOLED_ellipse);


static void fill_ellipse(amoled_AMOLED_obj_t *self, uint16_t xm, uint16_t ym, uint16_t rx, uint16_t ry, uint16_t color) {
	
	self->hold_display = true;

	//If ellipse is flat, général algorythm will fall into loop
	if ((rx == 0)|(ry == 0)){
		if (rx == 0) {
			fast_hline(self, xm - ry, ym, 2 * ry, color);
		}
		if (ry == 0){
			fast_vline(self, xm, ym-rx, 2 * rx, color);
		}
	}
	else {	
			
		int x = 0;
		int y = ry;
		
		//let to check vs int
		float d1 = (ry * ry) - (rx * rx * ry) + (0.25 * rx * rx);
		float dx = 0;
		float dy = 2 * rx * rx * y;

		self->hold_display = true;

		//If ellipse is flat, général algorythm will fall into loop
		if ((rx == 0)|(ry == 0)){
			if (rx == 0) {
				fast_hline(self, xm - ry, ym, 2 * ry, color);
			}
			if (ry == 0){
				fast_vline(self, xm, ym-rx, 2 * rx, color);
			}
		}
		else {

			// Plotting points of region 1	
			while (dx <= dy) {
				
				fast_vline(self, xm + x, ym - y, 2 * y, color);
				fast_vline(self, xm - x, ym - y, 2 * y, color);
			
				if (d1 < 0) {
					x++;
					dx = dx + (2 * ry * ry);
					d1 = d1 + dx + (ry * ry);
				} 
				else {
					x++;
					y--;
					dx = dx + (2 * ry * ry);
					dy = dy - (2 * rx * rx);
					d1 = d1 + dx - dy + (ry * ry);
				}
			}

			int d2 = ((ry * ry) * ((x + 0.5) * (x + 0.5))) + ((rx * rx) * ((y - 1) * (y - 1))) - (rx * rx * ry * ry);

			// Plotting points of region 2
			while (y >= 0) {
				
				fast_vline(self, xm + x, ym - y, 2 * y, color);
				fast_vline(self, xm - x, ym - y, 2 * y, color);
				
				if (d2 > 0) {
					y--;
					dy = dy - (2 * rx * rx);
					d2 = d2 + (rx * rx) - dy;
				} 
				else {
					y--;
					x++;
					dx = dx + (2 * ry * ry);
					dy = dy - (2 * rx * rx);
					d2 = d2 + dx - dy + (rx * rx);
				}
			}
		}
	}
	
	self->hold_display = false;
	refresh_display(self,xm-rx,ym-ry,2*rx+1,2*ry+1);
}

static mp_obj_t amoled_AMOLED_fill_ellipse(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint16_t xm = mp_obj_get_int(args[1]);
    uint16_t ym = mp_obj_get_int(args[2]);
    uint16_t rx = mp_obj_get_int(args[3]);
	uint16_t ry = mp_obj_get_int(args[4]);
    uint16_t color = mp_obj_get_int(args[5]);

    fill_ellipse(self, xm, ym, rx, ry, color);
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_fill_ellipse_obj, 6, 6, amoled_AMOLED_fill_ellipse);


// Return the center of a polygon as an (x, y) tuple
static mp_obj_t amoled_AMOLED_polygon_center(size_t n_args, const mp_obj_t *args) {
    size_t poly_len;
    mp_obj_t *polygon;
    mp_obj_get_array(args[1], &poly_len, &polygon);

    mp_float_t sum = 0.0;
    int vsx = 0;
    int vsy = 0;

    if (poly_len > 0) {
        for (int idx = 0; idx < poly_len; idx++) {
            size_t point_from_poly_len;
            mp_obj_t *point_from_poly;
            mp_obj_get_array(polygon[idx], &point_from_poly_len, &point_from_poly);
            if (point_from_poly_len < 2) {
                mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Polygon data error"));
            }

            mp_int_t v1x = mp_obj_get_int(point_from_poly[0]);
            mp_int_t v1y = mp_obj_get_int(point_from_poly[1]);

            mp_obj_get_array(polygon[(idx + 1) % poly_len], &point_from_poly_len, &point_from_poly);
            if (point_from_poly_len < 2) {
                mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Polygon data error"));
            }

            mp_int_t v2x = mp_obj_get_int(point_from_poly[0]);
            mp_int_t v2y = mp_obj_get_int(point_from_poly[1]);

            mp_float_t cross = v1x * v2y - v1y * v2x;
            sum += cross;
            vsx += (int)((v1x + v2x) * cross);
            vsy += (int)((v1y + v2y) * cross);
        }

        mp_float_t z = 1.0 / (3.0 * sum);
        vsx = (int)(vsx * z);
        vsy = (int)(vsy * z);
    } else {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Polygon data error"));
    }

    mp_obj_t center[2] = {mp_obj_new_int(vsx), mp_obj_new_int(vsy)};
    return mp_obj_new_tuple(2, center);
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_polygon_center_obj, 2, 2, amoled_AMOLED_polygon_center);


static void rotate_polygon(Polygon *polygon, Point center, mp_float_t angle) {
    if (polygon->length == 0) {
        return;         /* reject null polygons */

    }
    mp_float_t cosAngle = MICROPY_FLOAT_C_FUN(cos)(angle);
    mp_float_t sinAngle = MICROPY_FLOAT_C_FUN(sin)(angle);

    for (int i = 0; i < polygon->length; i++) {
        mp_float_t dx = (polygon->points[i].x - center.x);
        mp_float_t dy = (polygon->points[i].y - center.y);

        polygon->points[i].x = center.x + (int)0.5 + (dx * cosAngle - dy * sinAngle);
        polygon->points[i].y = center.y + (int)0.5 + (dx * sinAngle + dy * cosAngle);
    }
}


static mp_obj_t amoled_AMOLED_polygon(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);

    size_t poly_len;
    mp_obj_t *polygon;
    mp_obj_get_array(args[1], &poly_len, &polygon);
	uint16_t xmax;
	uint16_t xmin;
	uint16_t ymin;
	uint16_t ymax;
	uint16_t x0;
	uint16_t y0;
	uint16_t x1;
	uint16_t y1;
	
    self->work = NULL;

    if (poly_len > 0) {
        mp_int_t x = mp_obj_get_int(args[2]);
        mp_int_t y = mp_obj_get_int(args[3]);
        mp_int_t color = mp_obj_get_int(args[4]);

        mp_float_t angle = 0.0f;
        if (n_args > 5 && mp_obj_is_float(args[5])) {
            angle = mp_obj_float_get(args[5]);
        }

        mp_int_t cx = 0;
        mp_int_t cy = 0;

        if (n_args > 6) {
            cx = mp_obj_get_int(args[6]);
            cy = mp_obj_get_int(args[7]);
        }

        self->work = (void *) heap_caps_aligned_calloc(RAM_ALIGNMENT, poly_len, sizeof(Point), MALLOC_CAP_8BIT);
        if (self->work) {
            Point *point = (Point *)self->work;

            for (int idx = 0; idx < poly_len; idx++) {
                size_t point_from_poly_len;
                mp_obj_t *point_from_poly;
                mp_obj_get_array(polygon[idx], &point_from_poly_len, &point_from_poly);
                if (point_from_poly_len < 2) {
                    mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Polygon data error"));
                }

                mp_int_t px = mp_obj_get_int(point_from_poly[0]);
                mp_int_t py = mp_obj_get_int(point_from_poly[1]);
                point[idx].x = px;
                point[idx].y = py;
            }

            Point center;
            center.x = cx;
            center.y = cy;

            Polygon polygon;
            polygon.length = poly_len;
            polygon.points = self->work;

            if (angle > 0) {
                rotate_polygon(&polygon, center, angle);
            }

			xmax = (int)point[0].x + x;
			xmin = xmax;
			ymax = (int)point[0].y + y;
			ymin = ymax;
			
			self->hold_display = true;

            for (int idx = 1; idx < poly_len; idx++) {
				x0 = (int)point[idx - 1].x + x;
				y0 = (int)point[idx - 1].y + y;
				x1 = (int)point[idx].x + x;
				y1 = (int)point[idx].y + y;
				
				xmax = (x0>xmax) ? x0 : xmax;
				xmin = (x0<xmin) ? x0 : xmin;
				ymax = (y0>ymax) ? y0 : ymax;
				ymin = (y0<ymin) ? y0 : ymin;
				
                line(self,x0,y0,x1,y1, color);
            }
			
			self->hold_display = false;
			refresh_display(self,xmin,ymin,xmax-xmin,ymax-ymin);
			
            heap_caps_free(self->work);
            self->work = NULL;
        } else {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Polygon data error"));
        }
    } else {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Polygon data error"));
    }

    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_polygon_obj, 5, 8, amoled_AMOLED_polygon);


// public-domain code by Darel Rex Finley, 2007 https://alienryderflex.com/polygon_fill/ 
static void fill_polygon(amoled_AMOLED_obj_t *self, Polygon *polygon, Point location, uint16_t color) {
    int nodes, nodeX[MAX_POLY_CORNERS], pixelY, i, j, swap;

    int minX = INT_MAX;
    int maxX = INT_MIN;
    int minY = INT_MAX;
    int maxY = INT_MIN;

    for (i = 0; i < polygon->length; i++) {
        if (polygon->points[i].x < minX) {
            minX = (int)polygon->points[i].x;
        }

        if (polygon->points[i].x > maxX) {
            maxX = (int)polygon->points[i].x;
        }

        if (polygon->points[i].y < minY) {
            minY = (int)polygon->points[i].y;
        }

        if (polygon->points[i].y > maxY) {
            maxY = (int)polygon->points[i].y;
        }
    }

	self->hold_display = true;
    //  Loop through the rows
    for (pixelY = minY; pixelY < maxY; pixelY++) {
        //  Build a list of nodes.
        nodes = 0;
        j = polygon->length - 1;
        for (i = 0; i < polygon->length; i++) {
            if ((polygon->points[i].y < pixelY && polygon->points[j].y >= pixelY) ||
                (polygon->points[j].y < pixelY && polygon->points[i].y >= pixelY)) {
                if (nodes < MAX_POLY_CORNERS) {
                    nodeX[nodes++] = (int)(polygon->points[i].x +
                        (pixelY - polygon->points[i].y) /
                        (polygon->points[j].y - polygon->points[i].y) *
                        (polygon->points[j].x - polygon->points[i].x));
                } else {
                    mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Polygon too complex increase MAX_POLY_CORNERS."));
                }
            }
            j = i;
        }

        //  Sort the nodes, via a simple “Bubble” sort.
        i = 0;
        while (i < nodes - 1) {
            if (nodeX[i] > nodeX[i + 1]) {
                swap = nodeX[i];
                nodeX[i] = nodeX[i + 1];
                nodeX[i + 1] = swap;
                if (i) {
                    i--;
                }
            } else {
                i++;
            }
        }		
        //  Fill the pixels between node pairs.
        for (i = 0; i < nodes; i += 2) {
            if (nodeX[i] >= maxX) {
                break;
            }

            if (nodeX[i + 1] > minX) {
                if (nodeX[i] < minX) {
                    nodeX[i] = minX;
                }

                if (nodeX[i + 1] > maxX) {
                    nodeX[i + 1] = maxX;
                }

                fast_hline(self, (int)location.x + nodeX[i], (int)location.y + pixelY, nodeX[i + 1] - nodeX[i] + 1, color);
            }
        }
    }
	/*Adjust display refresh*/
	minX = minX + (int)location.x;
    maxX = maxX + (int)location.x;
    minY = minY + (int)location.y;
    maxY = maxY + (int)location.y;
	self->hold_display = false;	
	refresh_display(self,minX,minY,maxX - minX,maxY - minY);
}

static mp_obj_t amoled_AMOLED_fill_polygon(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);

    size_t poly_len;
    mp_obj_t *polygon;
    mp_obj_get_array(args[1], &poly_len, &polygon);

    self->work = NULL;

    if (poly_len > 0) {
        mp_int_t x = mp_obj_get_int(args[2]);
        mp_int_t y = mp_obj_get_int(args[3]);
        mp_int_t color = mp_obj_get_int(args[4]);

        mp_float_t angle = 0.0f;
        if (n_args > 5) {
            angle = mp_obj_float_get(args[5]);
        }

        mp_int_t cx = 0;
        mp_int_t cy = 0;

        if (n_args > 6) {
            cx = mp_obj_get_int(args[6]);
            cy = mp_obj_get_int(args[7]);
        }

        self->work = (void *) heap_caps_aligned_calloc(RAM_ALIGNMENT, poly_len, sizeof(Point), MALLOC_CAP_8BIT);
        if (self->work) {
            Point *point = (Point *)self->work;

            for (int idx = 0; idx < poly_len; idx++) {
                size_t point_from_poly_len;
                mp_obj_t *point_from_poly;
                mp_obj_get_array(polygon[idx], &point_from_poly_len, &point_from_poly);
                if (point_from_poly_len < 2) {
                    mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Polygon data error"));
                }

                point[idx].x = mp_obj_get_int(point_from_poly[0]);
                point[idx].y = mp_obj_get_int(point_from_poly[1]);
            }

            Point center = {cx, cy};
            Polygon polygon = {poly_len, self->work};

            if (angle != 0) {
                rotate_polygon(&polygon, center, angle);
            }

            Point location = {x, y};
            fill_polygon(self, &polygon, location, color);

            heap_caps_free(self->work);
            self->work = NULL;
        } else {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Polygon data error"));
        }

    } else {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Polygon data error"));
    }

    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_fill_polygon_obj, 5, 8, amoled_AMOLED_fill_polygon);


/*-----------------------------------------------------------------------------------------------------
Below are Monospaced fond related functions
------------------------------------------------------------------------------------------------------*/


//	text(font_module, string, x, y[, fg, bg])
static mp_obj_t amoled_AMOLED_text(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_obj_module_t *font = MP_OBJ_TO_PTR(args[1]);  		// Arg n°1 is the font pointer (font)
	const char *str_8 = (char *) mp_obj_str_get_str(args[2]);
	size_t str_8_len = strlen(str_8);
    mp_int_t x = mp_obj_get_int(args[3]);					// Arg n°3 is x_position x
    mp_int_t y = mp_obj_get_int(args[4]);					// Arg n°4 is y_position y
	mp_int_t fg_color = (n_args > 5) ? mp_obj_get_int(args[5]) : WHITE; // Arg 5 if front Color;
    mp_int_t bg_color  = (n_args > 6) ? mp_obj_get_int(args[6]) : BLACK; // Aarg 6 is back color;
	// if no Arg 6, we will not overwrite frame buffer	
	bool bg_filled = (n_args > 6) ? true : false;
	
	//Map font datas
    mp_obj_dict_t *dict = MP_OBJ_TO_PTR(font->globals);		// dict points to Font object (font)
    const uint8_t width = mp_obj_get_int(mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_WIDTH)));	 	// witdh is the font width
    const uint8_t height = mp_obj_get_int(mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_HEIGHT)));		// height...
    const uint8_t first = mp_obj_get_int(mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_FIRST)));		// first character
    const uint8_t last = mp_obj_get_int(mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_LAST)));			// last char.

    mp_obj_t font_data_buff = mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_FONT));						// font_data_buff is the font buff
    mp_buffer_info_t bufinfo;																			// bufinfo is the buffer interrupt
    mp_get_buffer_raise(font_data_buff, &bufinfo, MP_BUFFER_READ);										// 
    const uint8_t *font_data = bufinfo.buf;																// font_data is bufinfo.buf data pointer  

    uint8_t wide = width / 8; // wide = width in Bytes for a single char (ex 16bit large font is 2 bytes per line)
	mp_int_t x0 = x;
	char chr;		// String char	
	
	//Process every char
	for (uint8_t i = 0; i < str_8_len; i++) {
		chr = str_8[i];
        if (chr >= first && chr <= last) {	// if string character is in the font character range 
			if (x + width >= self->width) {
				return mp_const_none;  // return if char is away from dsplay
			}
            uint16_t chr_idx = (chr - first) * (height * wide);	// chr_index is the charactere index in the font file 
			size_t fram_buf_idx;  //bud_index is the framebuffer index
			for (uint8_t line = 0; line < height; line++) {		// for every line of the font character
				fram_buf_idx = (y + line) * self->width + x;	// buf_idx is the frame buffer start index for each line
				for (uint8_t line_byte = 0; line_byte < wide; line_byte++) { 	//for wide bytes of every line 
                    uint8_t chr_data = font_data[chr_idx];					 	// get corresponding data
                    for (uint8_t bit = 8; bit; bit--) {						 	// for every bits of the font
						if (chr_data >> (bit - 1) & 1) {	// 1 = Front color / 0 = back_color
                            self->fram_buf[fram_buf_idx] = fg_color;	
                        } else {
							if (bg_filled) { self->fram_buf[fram_buf_idx] = bg_color; }  //Fill background only if asked
                        }
                        fram_buf_idx++;	// next frame buffer index and proceed next font bit
                    }
                    chr_idx++;	// next font line_byte
                }																			
            }																// next line
            x += width;	 // next chart ==> x0 moves to next place
        }	// if not in font character range = Do nothing
    } // all source character proceeded
	refresh_display(self,x0,y,x - x0,height);
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_text_obj, 5, 7, amoled_AMOLED_text);


static mp_obj_t amoled_AMOLED_text_len(size_t n_args, const mp_obj_t *args) {
    //amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint8_t single_char_s;
    const uint8_t *source = NULL;
    size_t source_len = 0;

    // extract arguments
    mp_obj_module_t *font = MP_OBJ_TO_PTR(args[1]);  		// Arg n°1 is the font pointer (font)

    if (mp_obj_is_int(args[2])) {
        mp_int_t c = mp_obj_get_int(args[2]);    			// Arg n°2 is wether a 1 byte single caracter  (c)
        single_char_s = (c & 0xff);
        source = &single_char_s;
        source_len = 1;
    } else if (mp_obj_is_str(args[2])) { 					// or a sting
        source = (uint8_t *) mp_obj_str_get_str(args[2]);
        source_len = strlen((char *)source);
    } else if (mp_obj_is_type(args[2], &mp_type_bytes)) {	// or a byte_array
        mp_obj_t text_data_buff = args[2];
        mp_buffer_info_t text_bufinfo;
        mp_get_buffer_raise(text_data_buff, &text_bufinfo, MP_BUFFER_READ);	// text_bufinfo is activated text_data_buff receives data 
        source = text_bufinfo.buf;							// in every case the string is named source 
        source_len = text_bufinfo.len;						// its length is source_len
    } else {
        mp_raise_TypeError(MP_ERROR_TEXT("text requires either int, str or bytes."));
        return mp_const_none;
    }
	
    mp_obj_dict_t *dict = MP_OBJ_TO_PTR(font->globals);		// dict points to Font object (font)
    const uint8_t width = mp_obj_get_int(mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_WIDTH)));	 	// witdh is the font width
	
    uint16_t print_width = source_len * width;
	
    return mp_obj_new_int(print_width);
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_text_len_obj, 3, 3, amoled_AMOLED_text_len);


/*---------------------------------------------------------------------------------------------------
Below are Variable Fonts related functions
----------------------------------------------------------------------------------------------------*/


//	write(font_module, string, x, y[, fg, bg)
static mp_obj_t amoled_AMOLED_write(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_obj_module_t *font = MP_OBJ_TO_PTR(args[1]);
	const char *str_8 = (char *) mp_obj_str_get_str(args[2]);
	size_t str_8_len = strlen(str_8);
	mp_int_t x = mp_obj_get_int(args[3]);
	mp_int_t y = mp_obj_get_int(args[4]);
    mp_int_t fg_color = (n_args > 5) ? mp_obj_get_int(args[5]) : WHITE; // Arg 5 if front Color;
    mp_int_t bg_color  = (n_args > 6) ? mp_obj_get_int(args[6]) : BLACK; // Aarg 6 is back color;
	// if no Arg 6, we will not overwrite frame buffer	
	bool bg_filled = (n_args > 6) ? true : false;
	
	uint8_t *bitmap_data = NULL;
	
	//Map font datas
    mp_obj_dict_t *dict = MP_OBJ_TO_PTR(font->globals);
    const uint8_t height = mp_obj_get_int(mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_HEIGHT)));  // height is the font height
    const uint8_t offset_width = mp_obj_get_int(mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_OFFSET_WIDTH)));

    mp_obj_t widths_data_buff = mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_WIDTHS));  
    mp_buffer_info_t widths_bufinfo;
    mp_get_buffer_raise(widths_data_buff, &widths_bufinfo, MP_BUFFER_READ);
    const uint8_t *widths_data = widths_bufinfo.buf;	// widths_data is char by char width 

    mp_obj_t offsets_data_buff = mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_OFFSETS));
    mp_buffer_info_t offsets_bufinfo;
    mp_get_buffer_raise(offsets_data_buff, &offsets_bufinfo, MP_BUFFER_READ);
    const uint8_t *offsets_data = offsets_bufinfo.buf;  // offsets_data is char offset in data in order to reach each char data

    mp_obj_t bitmaps_data_buff = mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_BITMAPS));
    mp_buffer_info_t bitmaps_bufinfo;
    mp_get_buffer_raise(bitmaps_data_buff, &bitmaps_bufinfo, MP_BUFFER_READ);
    bitmap_data = bitmaps_bufinfo.buf; //bitmap_data is font data

    mp_obj_t map_obj = mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_MAP));
    GET_STR_DATA_LEN(map_obj, map_data, map_len);
	
	size_t fram_buf_idx;
	mp_int_t x0 = x;
	char chr;		// String char
	uint32_t bs_bit = 0;

	//Process every char
	for (uint8_t i = 0; i < str_8_len; i++) {
		chr = str_8[i];	

        const byte *map_s = map_data, *map_top = map_data + map_len;
        uint16_t char_index = 0;

        //Basic in line serch loop
		while (map_s < map_top) {
            unichar map_ch;
            map_ch = utf8_get_char(map_s);
            map_s = utf8_next_char(map_s);

			fram_buf_idx = 0;  // Init buffer index
			
			//If found get bit datas
            if (chr == map_ch) {
                uint8_t width = widths_data[char_index];    //width is the character width
				if (x + width >= self->width) {
					return mp_const_none;  // return if char is away from dsplay
				}
                bs_bit = 0; //bs_bit will point to the font character 1st bit; it can be offseted from 1 to 3 bits !
                switch (offset_width) {
                    case 1:
                        bs_bit = offsets_data[char_index * offset_width];
                        break;

                    case 2:
                        bs_bit = (offsets_data[char_index * offset_width] << 8) +
                            (offsets_data[char_index * offset_width + 1]);
                        break;

                    case 3:
                        bs_bit = (offsets_data[char_index * offset_width] << 16) +
                            (offsets_data[char_index * offset_width + 1] << 8) +
                            (offsets_data[char_index * offset_width + 2]);
                        break;
                }

				//Render to display		
                for (uint16_t line = 0; line < height; line++) {  // for every line of char	
					fram_buf_idx = (y + line) * self->width + x;	// buf_idx is the frame buffer start index for each line
                    for (uint16_t line_bits = 0; line_bits < width; line_bits++) { //for every bit of every line
						if ((bitmap_data[bs_bit / 8] & 1 << (7 - (bs_bit % 8)))) { //Check if pixel bit if 1 or 0
							self->fram_buf[fram_buf_idx] = fg_color;
						} else {
							if (bg_filled) { self->fram_buf[fram_buf_idx] = bg_color; }  //Fill background only if asked
						}
						bs_bit++;
						fram_buf_idx++;
                    }
				}			
                x += width;
                break;
            }
            char_index++;
        }
    }
    refresh_display(self,x0,y,x - x0,height);
	return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_write_obj, 5, 9, amoled_AMOLED_write);


//	write_len(font_module, string)
static mp_obj_t amoled_AMOLED_write_len(size_t n_args, const mp_obj_t *args) {
    mp_obj_module_t *font = MP_OBJ_TO_PTR(args[1]);
	//Map font properties
    mp_obj_dict_t *dict = MP_OBJ_TO_PTR(font->globals);
	mp_obj_t map_obj = mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_MAP));
    GET_STR_DATA_LEN(map_obj, map_data, map_len);
    mp_obj_t widths_data_buff = mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_WIDTHS));
    mp_buffer_info_t widths_bufinfo;
    mp_get_buffer_raise(widths_data_buff, &widths_bufinfo, MP_BUFFER_READ);
    const uint8_t *widths_data = widths_bufinfo.buf;

    uint16_t x = 0;
	const char *str_8 = (char *) mp_obj_str_get_str(args[2]);
	size_t str_8_len = strlen(str_8);
	char chr;		// String char

	//Process every char
	for (uint8_t i = 0; i < str_8_len; i++) {
		chr = str_8[i];

		//map_s & map_top are font char index min and max
        const byte *map_s = map_data, *map_top = map_data + map_len;
        uint16_t search_index = 0;

		//Basic in-line search
        while (map_s < map_top) {
            unichar map_ch;
            map_ch = utf8_get_char(map_s);
            map_s = utf8_next_char(map_s);

			//if found, increase caculated width
            if (chr == map_ch) {
                x += widths_data[search_index];
                break;
            }
            search_index++;
        }
    }

    return mp_obj_new_int(x);
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_write_len_obj, 3, 3, amoled_AMOLED_write_len);


/*---------------------------------------------------------------------------------------------------
Below are Hershey Vectorial Fonts related functions
----------------------------------------------------------------------------------------------------*/


//	draw(font, string , x, y[, fg, bg])
static mp_obj_t amoled_AMOLED_draw(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);	
    mp_obj_module_t *hershey = MP_OBJ_TO_PTR(args[1]);
	const char *str_8 = (char *) mp_obj_str_get_str(args[2]);
	size_t str_8_len = strlen(str_8);
    mp_int_t x = mp_obj_get_int(args[3]);
    mp_int_t y = mp_obj_get_int(args[4]);
    mp_int_t color = (n_args > 5) ? mp_obj_get_int(args[5]) : WHITE;

    mp_float_t scale = 1.0;
    if (n_args > 6) {
        if (mp_obj_is_float(args[6])) {
            scale = mp_obj_float_get(args[6]);
        }
        if (mp_obj_is_int(args[6])) {
            scale = (mp_float_t)mp_obj_get_int(args[6]);
        }
    }

	//Map Font properties
    mp_obj_dict_t *dict = MP_OBJ_TO_PTR(hershey->globals);
    mp_obj_t *index_data_buff = mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_INDEX));
    mp_buffer_info_t index_bufinfo;
    mp_get_buffer_raise(index_data_buff, &index_bufinfo, MP_BUFFER_READ);
    uint8_t *index = index_bufinfo.buf;
    mp_obj_t *font_data_buff = mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_FONT));
    mp_buffer_info_t font_bufinfo;
    mp_get_buffer_raise(font_data_buff, &font_bufinfo, MP_BUFFER_READ);
    int8_t *font = font_bufinfo.buf;

    int16_t from_x = x;
    int16_t from_y = y;
    int16_t to_x = x;
    int16_t to_y = y;
    int16_t pos_x = x;
    int16_t pos_y = y;
    bool penup = true;
    char c;
    int16_t ii;

	for (uint8_t i = 0; i < str_8_len; i++) {
    //while ((c = *s++)) {
			c = str_8[i];
			if (c >= 32 && c <= 127) {
				ii = (c - 32) * 2;

				int16_t offset = index[ii] | (index[ii + 1] << 8);
				int16_t length = font[offset++];
				int16_t left = (int)(scale * (font[offset++] - 0x52) + 0.5);
				int16_t right = (int)(scale * (font[offset++] - 0x52) + 0.5);
				int16_t width = right - left;

				if (length) {
					int16_t i;
					for (i = 0; i < length; i++) {
						if (font[offset] == ' ') {
							offset += 2;
							penup = true;
							continue;
						}

						int16_t vector_x = (int)(scale * (font[offset++] - 0x52) + 0.5);
						int16_t vector_y = (int)(scale * (font[offset++] - 0x52) + 0.5);

						if (!i || penup) {
							from_x = pos_x + vector_x - left;
							from_y = pos_y + vector_y;
						} else {
							to_x = pos_x + vector_x - left;
							to_y = pos_y + vector_y;

							line(self, from_x, from_y, to_x, to_y, color);
							from_x = to_x;
							from_y = to_y;
						}
						penup = false;
					}
				}
				pos_x += width;
			}
    }

    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_draw_obj, 5, 7, amoled_AMOLED_draw);


static mp_obj_t amoled_AMOLED_draw_len(size_t n_args, const mp_obj_t *args) {
    //amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]); // No use 
    mp_obj_module_t *hershey = MP_OBJ_TO_PTR(args[1]);
	const char *str_8 = (char *) mp_obj_str_get_str(args[2]);
	size_t str_8_len = strlen(str_8);

    mp_float_t scale = 1.0;
    if (n_args > 3) {
        if (mp_obj_is_float(args[3])) {
            scale = mp_obj_float_get(args[3]);
        }
        if (mp_obj_is_int(args[3])) {
            scale = (mp_float_t)mp_obj_get_int(args[3]);
        }
    }

	//Map font properties
    mp_obj_dict_t *dict = MP_OBJ_TO_PTR(hershey->globals);
    mp_obj_t *index_data_buff = mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_INDEX));
    mp_buffer_info_t index_bufinfo;
    mp_get_buffer_raise(index_data_buff, &index_bufinfo, MP_BUFFER_READ);
    uint8_t *index = index_bufinfo.buf;
    mp_obj_t *font_data_buff = mp_obj_dict_get(dict, MP_OBJ_NEW_QSTR(MP_QSTR_FONT));
    mp_buffer_info_t font_bufinfo;
    mp_get_buffer_raise(font_data_buff, &font_bufinfo, MP_BUFFER_READ);
    int8_t *font = font_bufinfo.buf;

    int16_t print_width = 0;
    char c;
    int16_t ii;

	for (uint8_t i = 0; i < str_8_len; i++) {
		c = str_8[i];
        if (c >= 32 && c <= 127) {
            ii = (c - 32) * 2;

            int16_t offset = (index[ii] | (index[ii + 1] << 8)) + 1;
            int16_t left =  font[offset++] - 0x52;
            int16_t right = font[offset++] - 0x52;
            int16_t width = right - left;
            print_width += width;
        }
    }

    return mp_obj_new_int((int)(print_width * scale + 0.5));
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_draw_len_obj, 3, 4, amoled_AMOLED_draw_len);


/*---------------------------------------------------------------------------------------------------
Below are schrift TTF related functions
----------------------------------------------------------------------------------------------------*/


/*
//Convert UTF32 from UTF8
static size_t utf8_to_utf32(const char *utf8, uint32_t *utf32, size_t max)
{
	uint32_t c;
	size_t i = 0;
	--max;
	while (*utf8) {
		if (i >= max)
			return 0;
		if (!(*utf8 & 0x80U)) {
			utf32[i++] = *utf8++;
		} else if ((*utf8 & 0xe0U) == 0xc0U) {
			c = (*utf8++ & 0x1fU) << 6;
			if ((*utf8 & 0xc0U) != 0x80U) return 0;
			utf32[i++] = c + (*utf8++ & 0x3fU);
		} else if ((*utf8 & 0xf0U) == 0xe0U) {
			c = (*utf8++ & 0x0fU) << 12;
			if ((*utf8 & 0xc0U) != 0x80U) return 0;
			c += (*utf8++ & 0x3fU) << 6;
			if ((*utf8 & 0xc0U) != 0x80U) return 0;
			utf32[i++] = c + (*utf8++ & 0x3fU);
		} else if ((*utf8 & 0xf8U) == 0xf0U) {
			c = (*utf8++ & 0x07U) << 18;
			if ((*utf8 & 0xc0U) != 0x80U) return 0;
			c += (*utf8++ & 0x3fU) << 12;
			if ((*utf8 & 0xc0U) != 0x80U) return 0;
			c += (*utf8++ & 0x3fU) << 6;
			if ((*utf8 & 0xc0U) != 0x80U) return 0;
			c += (*utf8++ & 0x3fU);
			if ((c & 0xFFFFF800U) == 0xD800U) return 0;
            utf32[i++] = c;
		} else return 0;
	}
	utf32[i] = 0;
	return i;
}
*/

//Create a font object holding the TTF and return the font object
mp_obj_t amoled_TTF_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {

	enum {
        ARG_ttf,
		ARG_kerning,
        ARG_xscale,
        ARG_yscale,
		ARG_ydonwward
    };
    const mp_arg_t make_new_args[] = {
        { MP_QSTR_ttf,			MP_ARG_OBJ  | MP_ARG_KW_ONLY | MP_ARG_REQUIRED	},
		{ MP_QSTR_kerning,		MP_ARG_BOOL | MP_ARG_KW_ONLY,  {.u_bool = true	}},
        { MP_QSTR_xscale,		MP_ARG_INT  | MP_ARG_KW_ONLY,  {.u_int = 16		}},
        { MP_QSTR_yscale,		MP_ARG_INT  | MP_ARG_KW_ONLY,  {.u_int = 16		}},
		{ MP_QSTR_ydonwward,    MP_ARG_INT  | MP_ARG_KW_ONLY,  {.u_int = 1		}},
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(make_new_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(make_new_args), make_new_args, args);

	// create new object
	SFT *self = m_new_obj(SFT);
	self->base.type = &amoled_TTF_type;
	
	const char *filename = mp_obj_str_get_str((void *) args[ARG_ttf].u_rom_obj);
	int32_t size=0;
	
	self->xScale    = args[ARG_xscale].u_int;
	self->yScale    = args[ARG_yscale].u_int;
	self->kerning 	= args[ARG_yscale].u_int;
	self->flags		= args[ARG_ydonwward].u_bool;
	
    //Create sft_font in SPIRAM
	if (!(self->font = heap_caps_malloc(sizeof self->font, MALLOC_CAP_8BIT))) {
		mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("Cannot allocate sft font."));
	}

	mp_file_t 	*fp;
	//Use Amoled file pointer to opren font file
	fp = mp_open(filename, "rb");	
	
	if (fp == NULL) {
		mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("Cannot open font file."));
	}
	
	if ((mp_seek(fp, 0, MP_SEEK_END) < 0) || ((size = mp_tell(fp)) < 0)) {
		mp_close(fp);
		mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("Cannot determine font file size."));
	}

	//Allocatate font memory buffer
	self->font->memory = heap_caps_aligned_alloc(RAM_ALIGNMENT, size + 1, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
	
	//Check if memory allocation was OK
	if(self->font->memory == NULL) {
		mp_close(fp);
		mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("Cannot allocate font memory."));
	}

	//Go back (as Rewind) to the beginning of fp
	mp_seek(fp, 0, MP_SEEK_SET);

	//Read nsize bytes of fp to data
	self->font->size = mp_readinto(fp, (void *) self->font->memory, size);
	
	if(self->font->size != size) {
		mp_close(fp);
		mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("Cannot read full file."));
	} else {
		//self->sft.font->memory[nread] = '\0';
		self->font->source = SrcMapping;
	}

	//Close the file
	mp_close(fp);
	
	//Proceed font initialisation
	if(init_font(self->font) != 0) {
		mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("Cannot initialize font."));
	} else {
		return MP_OBJ_FROM_PTR(self);
	}
}


//Scale font : scale(x_scale, y_scale) 
static mp_obj_t amoled_TTF_scale(size_t n_args, const mp_obj_t *args) {
	
    SFT *self = MP_OBJ_TO_PTR(args[0]);

	if (n_args > 1) {
		if (mp_obj_is_float(args[1])) {
            self->xScale = (double)mp_obj_float_get(args[1]);
        }
        if (mp_obj_is_int(args[1])) {
            self->xScale = (double)mp_obj_get_int(args[1]);
		}
	} else {
		mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("TTF scale need at least 1 int or float argument"));
	}
	
	if (n_args > 2) {
		if (mp_obj_is_float(args[2])) {
            self->yScale = (double)mp_obj_float_get(args[2]);
        }
        if (mp_obj_is_int(args[2])) {
            self->yScale = (double)mp_obj_get_int(args[2]);
		}
	} else {
		self->yScale = self->xScale;
	}

    return mp_const_none;		
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_TTF_scale_obj, 2, 3, amoled_TTF_scale);


//Draw a TTF text :  ttf_draw(font, string, x, y[, fg, bg])
static mp_obj_t amoled_AMOLED_ttf_draw(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
	SFT *sft = (SFT *) MP_OBJ_TO_PTR(args[1]);
	//Arg1 string and transform UTF32
	const char *str_8 = (char *) mp_obj_str_get_str(args[2]);
	size_t str_8_len = strlen(str_8);
	//Transform to UTF32
	//uint32_t str_32[str_8_len];
	//utf8_to_utf32(str_8, str_32, str_8_len);
	//Arg2&3 are positions
    mp_int_t x0 = mp_obj_get_int(args[3]);
    mp_int_t y0 = mp_obj_get_int(args[4]);
	// Arg 4 if front Color, White by default
    mp_int_t fg_color = (n_args > 5) ? mp_obj_get_int(args[5]) : WHITE; 
	// Arg 5 if back Color, if specified we will write over the frame buffer
	mp_int_t bg_color = (n_args > 6) ? mp_obj_get_int(args[6]) : BLACK;
	// if no Arg 6, we will not overwrite frame buffer	
	bool bg_filled = (n_args > 6) ? true : false;
	
	mp_int_t x_nextchar = x0;
	mp_int_t y_nextchar = y0;
	mp_int_t x_pen = x0;
	mp_int_t y_pen = y0;
	mp_int_t ymin = y0;
	mp_int_t ymax = y0;

	//Get bpp process for antialiasing process
	uint32_t 	fltr_col_rd = self->bpp_process.fltr_col_rd;
	uint8_t 	bitsw_col_rd = self->bpp_process.bitsw_col_rd;
	uint32_t	fltr_col_gr = self->bpp_process.fltr_col_gr;
	uint8_t 	bitsw_col_gr = self->bpp_process.bitsw_col_gr;
	uint32_t	fltr_col_bl = self->bpp_process.fltr_col_bl;

	//Process Fg color decomposition
	mp_int_t fg_color_sw = (fg_color >> 8) | (fg_color << 8); //Because of Little Indian
	mp_int_t fg_color_rd = (fg_color_sw & fltr_col_rd) >> bitsw_col_rd;
	mp_int_t fg_color_gr = (fg_color_sw & fltr_col_gr) >> bitsw_col_gr;
	mp_int_t fg_color_bl = (fg_color_sw & fltr_col_bl);
		
	// Variable to process color mitigation and final color
	mp_int_t mfg_color_rd = 0;
	mp_int_t mfg_color_gr = 0;
	mp_int_t mfg_color_bl = 0;
	mp_int_t mfg_color = 0;
	
	SFT_Glyph g_id;
	SFT_GMetrics g_mtx;
	SFT_Image g_img;
	SFT_Glyph left_glyph = 0;
	SFT_Kerning kerning = { .xShift=0, .yShift=0,};
	
	uint16_t gl_idx;  	// index for rendered glyph
	size_t fram_buf_idx;    	// index for frame buffer
	uint8_t gl_data;   	// temporary glyph pixel value
	//uint32_t chr;		// String char
	uint8_t chr;

	//Process every char
	for (uint8_t i = 0; i < str_8_len; i++) {
		//chr = str_32[i];
		chr = str_8[i];
		
		//Search the gliph_id within the Font
		if(sft_lookup(sft, chr, &g_id) < 0) {
			mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("TTF Unknown glyph"));
		}

		//Then Get Glyph Metrics
		if(sft_gmetrics(sft, g_id, &g_mtx) < 0) {
			mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("TTF Bad glyph metrics"));
		}
		
		//Check if need space correction (if kerning activated)
		if(sft->kerning && (left_glyph != 0)) {
			sft_kerning(sft, left_glyph, chr, &kerning);
			left_glyph = chr;  // Update last_glyph
		}
		
		//Setup the glyph image and render glyph
		g_img.width = (g_mtx.minWidth + 3) & ~3;  // round to closest upper value multiple of 4 (0,4,8,aso...)
		g_img.height = g_mtx.minHeight;
		uint8_t pixels[g_img.width * g_img.height];
		g_img.pixels = pixels;
		if(sft_render(sft, g_id, g_img) < 0) {
			mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("TTF Error SFT rendering"));
		}
		
		//Adjust char position with kerning
		x_nextchar += kerning.xShift;		// Correction of x coordonates for next char 
		y_nextchar = y0 + kerning.yShift;	// 
		
		//Set pen position from nextchar position and glyph coodonate
		x_pen = x_nextchar + g_mtx.leftSideBearing;
		y_pen = y_nextchar + g_mtx.yOffset;
		 
		//Update Y min and max, will help diplay refresh later
		ymin = min_val(ymin , y_pen);
		ymax = max_val(ymax , y_pen + g_img.height);
			
		//Now put the Glyph to the display frame_buffer	
		gl_idx = 0;  //Glyph pointer set to 0 at beginning
		for (uint16_t y_gly = 0; y_gly < g_img.height; y_gly++) {		// for every line of the glyph
			fram_buf_idx = (y_pen + y_gly) * self->width + x_pen;			// fram_buf_idx is the frame buffer start index for each line
			for (uint16_t x_gly = 0; x_gly < g_img.width; x_gly++) {	// for every cols of the glyph
				gl_data = g_img.pixels[gl_idx];		                	// get glyph pixel value (1 Byte)
	
				switch (gl_data) {
					case 255 : // If full 255 => Plain Fg color
						self->fram_buf[fram_buf_idx] = fg_color;
					break;

					case 0 :  // If 0 => Bg color
						if (bg_filled) { self->fram_buf[fram_buf_idx] = bg_color; }
					break;

					default:	//Otherwise, moderate color if aliasing activated
						mfg_color_rd = ((gl_data * fg_color_rd) >> 8) << bitsw_col_rd;
						mfg_color_gr = ((gl_data * fg_color_gr) >> 8) << bitsw_col_gr;
						mfg_color_bl = (gl_data * fg_color_bl) >> 8;
						mfg_color = ( mfg_color_rd | mfg_color_gr | mfg_color_bl);
						self->fram_buf[fram_buf_idx] = (uint16_t) (mfg_color >> 8) | (mfg_color << 8); //Because of little indian
					break;
				}
				fram_buf_idx++;    // Next framebuffer pixel
				gl_idx ++;	  // Next glyph pixel
			}
		}
		x_nextchar += g_mtx.advanceWidth;    // next glyph must adwvance 
	}
	
	//Now refresh the display from the frame_buffer (x,y,w,h)
	refresh_display(self,x0, ymin, x_nextchar - x0 , ymax - ymin);

    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_ttf_draw_obj, 5, 7, amoled_AMOLED_ttf_draw);


//Get TTF drawn text length :  ttf_len(font, string) with font pointer
static mp_obj_t amoled_AMOLED_ttf_len(size_t n_args, const mp_obj_t *args) {
    //amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
	SFT *sft = (SFT *) MP_OBJ_TO_PTR(args[1]);
	//Arg1 string and transform UTF32
	const char *str_8 = (char *) mp_obj_str_get_str(args[2]);
	size_t str_8_len = strlen(str_8);
	//Transform to UTF32
	//uint32_t str_32[str_8_len];
	//utf8_to_utf32(str_8, str_32, str_8_len);
	//Arg2&3 are positions

	mp_int_t x_nextchar = 0;

	SFT_Glyph g_id;
	SFT_GMetrics g_mtx;
	SFT_Glyph left_glyph = 0;
	SFT_Kerning kerning = { .xShift=0, .yShift=0,};
	
	uint8_t chr;

	//Process every char
	for (uint8_t i = 0; i < str_8_len; i++) {
		//chr = str_32[i];
		chr = str_8[i];
		
		//Search the gliph_id within the Font
		if(sft_lookup(sft, chr, &g_id) < 0) {
			mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("TTF Unknown glyph"));
		}

		//Then Get Glyph Metrics
		if(sft_gmetrics(sft, g_id, &g_mtx) < 0) {
			mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("TTF Bad glyph metrics"));
		}
		
		//Check if need space correction (if kerning activated)
		if(sft->kerning && (left_glyph != 0)) {
			sft_kerning(sft, left_glyph, chr, &kerning);
			left_glyph = chr;  // Update last_glyph
		}
				
		//Adjust char position with kerning
		x_nextchar += kerning.xShift;		// Correction of x coordonates for next char 
					
		x_nextchar += g_mtx.advanceWidth;    // next glyph must advance 
	}
	
    return mp_obj_new_int((int)x_nextchar);
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_ttf_len_obj, 3, 3, amoled_AMOLED_ttf_len);


/*-----------------------------------------------------------------------------------------------------
Below are bitmap related functions
------------------------------------------------------------------------------------------------------*/


//bitmap(self,x0,y0,x1,y1,bitmap)
static mp_obj_t amoled_AMOLED_bitmap(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);

    int x_start = mp_obj_get_int(args[1]);
    int y_start = mp_obj_get_int(args[2]);
    int x_end   = mp_obj_get_int(args[3]);
    int y_end   = mp_obj_get_int(args[4]);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[5], &bufinfo, MP_BUFFER_READ);
    set_area(self, x_start, y_start, x_end, y_end);
    size_t len = ((x_end - x_start) * (y_end - y_start) * self->Bpp);
    write_color(self, bufinfo.buf, len);

    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_bitmap_obj, 6, 6, amoled_AMOLED_bitmap);


/*----------------------------------------------------------------------------------------------------
Below are JPG related functions.
-----------------------------------------------------------------------------------------------------*/


/* file input function returns number of bytes read (zero on error)
jd = Decompression object, buff = Pointer to read buffer, nbytes = Number of bytes to read/remove*/

static unsigned int in_func(JDEC *jd, uint8_t *buff, unsigned int nbyte) {               
    IODEV *dev = (IODEV *)jd->device;   // Device identifier for the session (5th argument of jd_prepare function)
    unsigned int nread;

    // Read data from input stream
    if (buff) {
        nread = (unsigned int)mp_readinto(dev->fp, buff, nbyte);
        return nread;
    }

    // Remove data from input stream if buff was NULL
    mp_seek(dev->fp, nbyte, SEEK_CUR);
    return 0;
}

// fast output function returns 1:Ok, 0:Aborted
// jd = Decompression object, bitmap = Bitmap data to be output, rect = Rectangular region of output image

static int out_fast(JDEC *jd,void *bitmap, JRECT *rect) {
    IODEV *dev = (IODEV *)jd->device;
    uint8_t *src, *dst;
    uint16_t y, bws, bwd;

    // Copy the decompressed RGB rectangular to the frame buffer (assuming RGB565)
    src = (uint8_t *)bitmap;
    dst = dev->fbuf + 2 * (rect->top * dev->wfbuf + rect->left);    // Left-top of destination rectangular assuming 16bpp = 2 bytes
    bws = 2 * (rect->right - rect->left + 1);                       // Width of source rectangular [byte]
    bwd = 2 * dev->wfbuf;                                           // Width of frame buffer [byte]
    for (y = rect->top; y <= rect->bottom; y++) {
        memcpy(dst, src, bws);                                      // Copy a line
        src += bws;
        dst += bwd;                                                 // Next line
    }
    return 1;     // Continue to decompress
}


// Draw jpg from a file at x, y
static mp_obj_t amoled_AMOLED_jpg(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
	const char *filename = mp_obj_str_get_str(args[1]);
	mp_int_t x = mp_obj_get_int(args[2]);
	mp_int_t y = mp_obj_get_int(args[3]);

    int (*outfunc)(JDEC *, void *, JRECT *);

    JRESULT res;	// Result code of TJpgDec API
    JDEC jdec;		// Decompression object
    self->work = (void *)heap_caps_aligned_alloc(RAM_ALIGNMENT, MAX_BUFFER, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);	// Pointer to the work area
	IODEV  devid;	// User defined device identifier
    size_t temp_buf_size;
	
	devid.fp = mp_open(filename, "rb");
	if (devid.fp) {
		// Prepare to decompress
		res = jd_prepare(&jdec, in_func, self->work, MAX_BUFFER, &devid);
		if (res == JDR_OK) {
			// Initialize output device
			temp_buf_size = 2 * jdec.width * jdec.height;
			outfunc = out_fast;
			
			self->temp_buf = heap_caps_aligned_alloc(RAM_ALIGNMENT, temp_buf_size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
			
			if (!self->temp_buf)
				mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("JPG error while allocating memory"));

			devid.fbuf	= (uint8_t *) self->temp_buf;
			//devid.fbuf	= (uint16_t *) self->temp_buf;
			devid.wfbuf = jdec.width;
			devid.self	= self;
			res			= jd_decomp(&jdec, outfunc, 0); // Start to decompress with 1/1 scaling
			
			if (res == JDR_OK) {
				
				size_t jpg_idx=0;
				size_t fram_buf_idx=0;
				uint16_t color;
				
				//Copy decompressed JPG from DEVID to Frame Buffer
				
				for(uint16_t line=0; line < jdec.height; line++) {
					fram_buf_idx = (y + line)*self->width + x;
					for(uint16_t col=0; col < jdec.width; col++) {
						color = devid.fbuf[jpg_idx+1] << 8 | devid.fbuf[jpg_idx];
						self->fram_buf[fram_buf_idx] = color;
						fram_buf_idx++;
						jpg_idx += 2;
					}
				}
			} else {
				mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("JPG decompression error"));
			}
			
			heap_caps_free((void*)self->temp_buf); // Discard frame buffer
			self->temp_buf = NULL;
			devid.fbuf = NULL;
			
		} else {
			mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("JPG preparation failed."));
		}
		mp_close(devid.fp);
	}
	heap_caps_free(self->work); // Discard work area
	//Refresh display (whole display for now)
	refresh_display(self,0,0,self->width,self->height);
	//refresh_display(self,x,y,jdec.width,jdec.height);
	return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_jpg_obj, 4, 4, amoled_AMOLED_jpg);

// output function for jpg_decode
// Retuns 1:Ok, 0:Aborted
// jd = Decompression object
// bitmap = Bitmap data to be output
// rect = Rectangular region of output image

static int out_crop(JDEC *jd, void *bitmap, JRECT *rect) {                      
    IODEV *dev = (IODEV *)jd->device;

    if (dev->left <= rect->right &&
        dev->right >= rect->left &&
        dev->top <= rect->bottom &&
        dev->bottom >= rect->top) {
			uint16_t left = MAX(dev->left, rect->left);
			uint16_t top = MAX(dev->top, rect->top);
			uint16_t right = MIN(dev->right, rect->right);
			uint16_t bottom = MIN(dev->bottom, rect->bottom);
			uint16_t dev_width = dev->right - dev->left + 1;
			uint16_t rect_width = rect->right - rect->left + 1;
			uint16_t width = (right - left + 1) * 2;
			uint16_t row;

			for (row = top; row <= bottom; row++) {
				memcpy(
					(uint16_t *)dev->fbuf + ((row - dev->top) * dev_width) + left - dev->left,
					(uint16_t *)bitmap + ((row - rect->top) * rect_width) + left - rect->left,
					width);
			}
	}
    return 1;     // Continue to decompress
}

// Decode a jpg file and return it or a portion of it as a tuple containing a blittable buffer, the width and height of the buffer.
static mp_obj_t amoled_AMOLED_jpg_decode(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
	const char	*filename;
	mp_int_t	x = 0, y = 0, width = 0, height = 0;

	if (n_args == 2 || n_args == 6) {
		filename = mp_obj_str_get_str(args[1]);
		if (n_args == 6) {
			x	   = mp_obj_get_int(args[2]);
			y	   = mp_obj_get_int(args[3]);
			width  = mp_obj_get_int(args[4]);
			height = mp_obj_get_int(args[5]);
		}
		self->work = (void *) heap_caps_aligned_alloc(RAM_ALIGNMENT, MAX_BUFFER, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM); // Pointer to the work area

		JRESULT res;   // Result code of TJpgDec API
		JDEC	jdec;  // Decompression object
		IODEV	devid; // User defined device identifier
		size_t	temp_buf_size = 0;

		devid.fp = mp_open(filename, "rb");
		if (devid.fp) {
			// Prepare to decompress
			res = jd_prepare(&jdec, in_func, self->work, MAX_BUFFER, &devid);
			if (res == JDR_OK) {
				if (n_args < 6) {
					x	   = 0;
					y	   = 0;
					width  = jdec.width;
					height = jdec.height;
				}
				// Initialize output device
				devid.left	 = x;
				devid.top	 = y;
				devid.right	 = x + width - 1;
				devid.bottom = y + height - 1;

				temp_buf_size			   = 2 * width * height;
				self->temp_buf = heap_caps_aligned_alloc(RAM_ALIGNMENT, temp_buf_size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
				if (self->temp_buf) {
					memset(self->temp_buf, 0xBEEF, temp_buf_size);
				} else {
					mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("out of memory"));
				}

				devid.fbuf	= (uint8_t *) self->temp_buf;
				devid.wfbuf = jdec.width;
				devid.self	= self;
				res			= jd_decomp(&jdec, out_crop, 0); // Start to decompress with 1/1 scaling
				if (res != JDR_OK) {
					mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("jpg decompress failed."));
				}

			} else {
				mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("jpg prepare failed."));
			}
			mp_close(devid.fp);
		}
		heap_caps_free(self->work); // Discard work area

		mp_obj_t result[3] = {
			mp_obj_new_bytearray(temp_buf_size, (mp_obj_t *) self->temp_buf),
			mp_obj_new_int(width),
			mp_obj_new_int(height)};

		return mp_obj_new_tuple(3, result);
	}

	mp_raise_TypeError(MP_ERROR_TEXT("jpg_decode requires either 2 or 6 arguments"));
	return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_jpg_decode_obj, 2, 6, amoled_AMOLED_jpg_decode);


/*---------------------------------------------------------------------------------------------------
Below are screencontroler related functions
----------------------------------------------------------------------------------------------------*/


static mp_obj_t amoled_AMOLED_mirror(mp_obj_t self_in, mp_obj_t mirror_x_in, mp_obj_t mirror_y_in) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (mp_obj_is_true(mirror_x_in)) {
        self->madctl_val |= MADCTL_MX_BIT;
    } else {
        self->madctl_val &= ~MADCTL_MX_BIT;
    }
    if (mp_obj_is_true(mirror_y_in)) {
        self->madctl_val |= MADCTL_MY_BIT;
    } else {
        self->madctl_val &= ~MADCTL_MY_BIT;
    }
    write_spi(self, LCD_CMD_MADCTL, (uint8_t[]) { self->madctl_val }, 1);
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_3(amoled_AMOLED_mirror_obj, amoled_AMOLED_mirror);


static mp_obj_t amoled_AMOLED_swap_xy(mp_obj_t self_in, mp_obj_t swap_axes_in) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (mp_obj_is_true(swap_axes_in)) {
        self->madctl_val |= MADCTL_MV_BIT;
    } else {
        self->madctl_val &= ~MADCTL_MV_BIT;
    }
    write_spi(self, LCD_CMD_MADCTL, (uint8_t[]) { self->madctl_val }, 1);
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_2(amoled_AMOLED_swap_xy_obj, amoled_AMOLED_swap_xy);

/*
static mp_obj_t amoled_AMOLED_set_gap(mp_obj_t self_in, mp_obj_t x_gap_in, mp_obj_t y_gap_in) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(self_in);
    self->col_start = mp_obj_get_int(x_gap_in);
    self->row_start = mp_obj_get_int(y_gap_in);
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_3(amoled_AMOLED_set_gap_obj, amoled_AMOLED_set_gap);*/


static mp_obj_t amoled_AMOLED_invert_color(mp_obj_t self_in, mp_obj_t invert_in) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (mp_obj_is_true(invert_in)) {
        write_spi(self, LCD_CMD_INVON, NULL, 0);
    } else {
        write_spi(self, LCD_CMD_INVOFF, NULL, 0);
    }
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_2(amoled_AMOLED_invert_color_obj, amoled_AMOLED_invert_color);


static mp_obj_t amoled_AMOLED_disp_off(mp_obj_t self_in) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(self_in);
    write_spi(self, LCD_CMD_SLPIN, NULL, 0);
    write_spi(self, LCD_CMD_DISPOFF, NULL, 0);
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_1(amoled_AMOLED_disp_off_obj, amoled_AMOLED_disp_off);


static mp_obj_t amoled_AMOLED_disp_on(mp_obj_t self_in) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(self_in);
    write_spi(self, LCD_CMD_SLPOUT, NULL, 0);
    write_spi(self, LCD_CMD_DISPON, NULL, 0);
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_1(amoled_AMOLED_disp_on_obj, amoled_AMOLED_disp_on);


static mp_obj_t amoled_AMOLED_backlight_on(mp_obj_t self_in) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(self_in);
    write_spi(self, LCD_CMD_WRDISBV, (uint8_t[]) { 0xFF }, 1);
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_1(amoled_AMOLED_backlight_on_obj, amoled_AMOLED_backlight_on);


static mp_obj_t amoled_AMOLED_backlight_off(mp_obj_t self_in) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(self_in);
    write_spi(self, LCD_CMD_WRDISBV, (uint8_t[]) { 0x00 }, 1);
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_1(amoled_AMOLED_backlight_off_obj, amoled_AMOLED_backlight_off);


static mp_obj_t amoled_AMOLED_brightness(mp_obj_t self_in, mp_obj_t brightness_in) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t brightness = mp_obj_get_int(brightness_in);

    if (brightness > 255) {
        brightness = 255;
    } else if (brightness < 0) {
        brightness = 0;
    }
    write_spi(self, LCD_CMD_WRDISBV, (uint8_t[]) { brightness & 0xFF }, 1);
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_2(amoled_AMOLED_brightness_obj, amoled_AMOLED_brightness);


static mp_obj_t amoled_AMOLED_width(mp_obj_t self_in) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(self->width);
}

static MP_DEFINE_CONST_FUN_OBJ_1(amoled_AMOLED_width_obj, amoled_AMOLED_width);


static mp_obj_t amoled_AMOLED_height(mp_obj_t self_in) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(self->height);
}

static MP_DEFINE_CONST_FUN_OBJ_1(amoled_AMOLED_height_obj, amoled_AMOLED_height);


//Setup display rotation, 3rd argument is optional and might be a tupple replacing default array
static mp_obj_t amoled_AMOLED_rotation(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    self->rotation = mp_obj_get_int(args[1]) % 4;
    if (n_args > 2) {
        mp_obj_tuple_t *rotations_in = MP_OBJ_TO_PTR(args[2]);
        for (size_t i = 0; i < rotations_in->len; i++) {
            if (i < 4) {
                mp_obj_tuple_t *item = MP_OBJ_TO_PTR(rotations_in->items[i]);
                self->rotations[i].madctl   = mp_obj_get_int(item->items[0]);
                self->rotations[i].width    = mp_obj_get_int(item->items[1]);
                self->rotations[i].height   = mp_obj_get_int(item->items[2]);
                self->rotations[i].colstart = mp_obj_get_int(item->items[3]);
                self->rotations[i].rowstart = mp_obj_get_int(item->items[4]);
            }
        }
    }
    set_rotation(self, self->rotation);
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_rotation_obj, 2, 3, amoled_AMOLED_rotation);


static void set_tearing(amoled_AMOLED_obj_t *self, uint8_t te, uint16_t scanline) {

	uint8_t bufte[1] = { te };
	uint8_t bufsl[2] = { (scanline >> 8), (scanline & 0xFF) };
	
	write_spi(self, LCD_CMD_TEON, bufte, 1);
	write_spi(self, LCD_CMD_SETTSCANL, bufsl, 2);

}

//Setup tearing(0/1, [scanline])
static mp_obj_t amoled_AMOLED_tearing(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    self->te = mp_obj_get_int(args[1]);
    if (n_args > 2) {
		self->scanline = mp_obj_get_int(args[2]);
	} else {
		self->scanline = self->height;
	}
    set_tearing(self, self->te, self->scanline);
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_tearing_obj, 2, 3, amoled_AMOLED_tearing);



static mp_obj_t amoled_AMOLED_vscroll_area(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_int_t tfa = mp_obj_get_int(args[1]);
    mp_int_t vsa = mp_obj_get_int(args[2]);
    mp_int_t bfa = mp_obj_get_int(args[3]);

    write_spi(
            self,
            LCD_CMD_VSCRDEF,
            (uint8_t []) {
                (tfa) >> 8,
                (tfa) & 0xFF,
                (vsa) >> 8,
                (vsa) & 0xFF,
                (bfa) >> 8,
                (bfa) & 0xFF
            },
            6
    );
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_vscroll_area_obj, 4, 4, amoled_AMOLED_vscroll_area);


static mp_obj_t amoled_AMOLED_vscroll_start(size_t n_args, const mp_obj_t *args) {
    amoled_AMOLED_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_int_t vssa = mp_obj_get_int(args[1]);

    if (n_args > 2) {
        if (mp_obj_is_true(args[2])) {
            self->madctl_val |= MADCTL_ML_BIT;
        } else {
            self->madctl_val &= ~MADCTL_ML_BIT;
        }
    } else {
        self->madctl_val &= ~MADCTL_ML_BIT;
    }
    write_spi(
        self,
        LCD_CMD_MADCTL,
        (uint8_t[]) { self->madctl_val, },
        2
    );

    write_spi(
        self,
        LCD_CMD_VSCSAD,
        (uint8_t []) { (vssa) >> 8, (vssa) & 0xFF },
        2
    );

    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(amoled_AMOLED_vscroll_start_obj, 2, 3, amoled_AMOLED_vscroll_start);


/*---------------------------------------------------------------------------------------------------
Below are C to Micropython library dictionnary
----------------------------------------------------------------------------------------------------*/

//amoled.AMOLED dictionnary

static const mp_rom_map_elem_t amoled_AMOLED_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_version),         MP_ROM_PTR(&amoled_AMOLED_version_obj)         },
    { MP_ROM_QSTR(MP_QSTR_deinit),          MP_ROM_PTR(&amoled_AMOLED_deinit_obj)          },
    { MP_ROM_QSTR(MP_QSTR_reset),           MP_ROM_PTR(&amoled_AMOLED_reset_obj)           },
    { MP_ROM_QSTR(MP_QSTR_init),            MP_ROM_PTR(&amoled_AMOLED_init_obj)            },
    { MP_ROM_QSTR(MP_QSTR_send_cmd),        MP_ROM_PTR(&amoled_AMOLED_send_cmd_obj)        },
    { MP_ROM_QSTR(MP_QSTR_refresh),         MP_ROM_PTR(&amoled_AMOLED_refresh_obj)         },
    { MP_ROM_QSTR(MP_QSTR_pixel),           MP_ROM_PTR(&amoled_AMOLED_pixel_obj)           },
    { MP_ROM_QSTR(MP_QSTR_fill),            MP_ROM_PTR(&amoled_AMOLED_fill_obj)            },
	{ MP_ROM_QSTR(MP_QSTR_line),            MP_ROM_PTR(&amoled_AMOLED_line_obj)            },
    { MP_ROM_QSTR(MP_QSTR_hline),           MP_ROM_PTR(&amoled_AMOLED_hline_obj)           },
    { MP_ROM_QSTR(MP_QSTR_vline),           MP_ROM_PTR(&amoled_AMOLED_vline_obj)           },
    { MP_ROM_QSTR(MP_QSTR_rect),            MP_ROM_PTR(&amoled_AMOLED_rect_obj)            },
    { MP_ROM_QSTR(MP_QSTR_fill_rect),       MP_ROM_PTR(&amoled_AMOLED_fill_rect_obj)       },
    { MP_ROM_QSTR(MP_QSTR_bubble_rect),     MP_ROM_PTR(&amoled_AMOLED_bubble_rect_obj)     },
    { MP_ROM_QSTR(MP_QSTR_fill_bubble_rect),MP_ROM_PTR(&amoled_AMOLED_fill_bubble_rect_obj)},
    { MP_ROM_QSTR(MP_QSTR_circle),          MP_ROM_PTR(&amoled_AMOLED_circle_obj)          },
    { MP_ROM_QSTR(MP_QSTR_fill_circle),     MP_ROM_PTR(&amoled_AMOLED_fill_circle_obj)     },
    { MP_ROM_QSTR(MP_QSTR_ellipse),         MP_ROM_PTR(&amoled_AMOLED_ellipse_obj)         },
    { MP_ROM_QSTR(MP_QSTR_fill_ellipse),    MP_ROM_PTR(&amoled_AMOLED_fill_ellipse_obj)    },
	{ MP_ROM_QSTR(MP_QSTR_trian),           MP_ROM_PTR(&amoled_AMOLED_trian_obj)           },
	{ MP_ROM_QSTR(MP_QSTR_fill_trian),      MP_ROM_PTR(&amoled_AMOLED_fill_trian_obj)      },
    { MP_ROM_QSTR(MP_QSTR_polygon),         MP_ROM_PTR(&amoled_AMOLED_polygon_obj)         },
    { MP_ROM_QSTR(MP_QSTR_fill_polygon),    MP_ROM_PTR(&amoled_AMOLED_fill_polygon_obj)    },
    { MP_ROM_QSTR(MP_QSTR_polygon_center),  MP_ROM_PTR(&amoled_AMOLED_polygon_center_obj)  },
    { MP_ROM_QSTR(MP_QSTR_colorRGB),        MP_ROM_PTR(&amoled_AMOLED_colorRGB_obj)        },
    { MP_ROM_QSTR(MP_QSTR_bitmap),          MP_ROM_PTR(&amoled_AMOLED_bitmap_obj)          },
    { MP_ROM_QSTR(MP_QSTR_jpg),             MP_ROM_PTR(&amoled_AMOLED_jpg_obj)             },
    { MP_ROM_QSTR(MP_QSTR_jpg_decode),      MP_ROM_PTR(&amoled_AMOLED_jpg_decode_obj)      },
    { MP_ROM_QSTR(MP_QSTR_text),            MP_ROM_PTR(&amoled_AMOLED_text_obj)            },
    { MP_ROM_QSTR(MP_QSTR_text_len),        MP_ROM_PTR(&amoled_AMOLED_text_len_obj)        },
    { MP_ROM_QSTR(MP_QSTR_write),           MP_ROM_PTR(&amoled_AMOLED_write_obj)           },
    { MP_ROM_QSTR(MP_QSTR_write_len),       MP_ROM_PTR(&amoled_AMOLED_write_len_obj)       },
    { MP_ROM_QSTR(MP_QSTR_draw),            MP_ROM_PTR(&amoled_AMOLED_draw_obj)            },
    { MP_ROM_QSTR(MP_QSTR_draw_len),        MP_ROM_PTR(&amoled_AMOLED_draw_len_obj)        },
	{ MP_ROM_QSTR(MP_QSTR_ttf_draw),   		MP_ROM_PTR(&amoled_AMOLED_ttf_draw_obj)        },
	{ MP_ROM_QSTR(MP_QSTR_ttf_len),   		MP_ROM_PTR(&amoled_AMOLED_ttf_len_obj)         },	
    { MP_ROM_QSTR(MP_QSTR_mirror),          MP_ROM_PTR(&amoled_AMOLED_mirror_obj)          },
    { MP_ROM_QSTR(MP_QSTR_swap_xy),         MP_ROM_PTR(&amoled_AMOLED_swap_xy_obj)         },
//    { MP_ROM_QSTR(MP_QSTR_set_gap),         MP_ROM_PTR(&amoled_AMOLED_set_gap_obj)         },
    { MP_ROM_QSTR(MP_QSTR_invert_color),    MP_ROM_PTR(&amoled_AMOLED_invert_color_obj)    },
    { MP_ROM_QSTR(MP_QSTR_disp_off),        MP_ROM_PTR(&amoled_AMOLED_disp_off_obj)        },
    { MP_ROM_QSTR(MP_QSTR_disp_on),         MP_ROM_PTR(&amoled_AMOLED_disp_on_obj)         },
    { MP_ROM_QSTR(MP_QSTR_backlight_on),    MP_ROM_PTR(&amoled_AMOLED_backlight_on_obj)    },
    { MP_ROM_QSTR(MP_QSTR_backlight_off),   MP_ROM_PTR(&amoled_AMOLED_backlight_off_obj)   },
    { MP_ROM_QSTR(MP_QSTR_brightness),      MP_ROM_PTR(&amoled_AMOLED_brightness_obj)      },
    { MP_ROM_QSTR(MP_QSTR_height),          MP_ROM_PTR(&amoled_AMOLED_height_obj)          },
    { MP_ROM_QSTR(MP_QSTR_width),           MP_ROM_PTR(&amoled_AMOLED_width_obj)           },
    { MP_ROM_QSTR(MP_QSTR_rotation),        MP_ROM_PTR(&amoled_AMOLED_rotation_obj)        },
    { MP_ROM_QSTR(MP_QSTR_tearing),         MP_ROM_PTR(&amoled_AMOLED_tearing_obj)        },	
    { MP_ROM_QSTR(MP_QSTR_vscroll_area),    MP_ROM_PTR(&amoled_AMOLED_vscroll_area_obj)    },
    { MP_ROM_QSTR(MP_QSTR_vscroll_start),   MP_ROM_PTR(&amoled_AMOLED_vscroll_start_obj)   },
    { MP_ROM_QSTR(MP_QSTR___del__),         MP_ROM_PTR(&amoled_AMOLED_deinit_obj)          },
    { MP_ROM_QSTR(MP_QSTR_RGB),             MP_ROM_INT(COLOR_SPACE_RGB)                    },
    { MP_ROM_QSTR(MP_QSTR_BGR),             MP_ROM_INT(COLOR_SPACE_BGR)                    },
    { MP_ROM_QSTR(MP_QSTR_MONOCHROME),      MP_ROM_INT(COLOR_SPACE_MONOCHROME)             },
};

static MP_DEFINE_CONST_DICT(amoled_AMOLED_locals_dict, amoled_AMOLED_locals_dict_table);

//amoled.TTF dictionnary
static const mp_rom_map_elem_t amoled_TTF_locals_dict_table[] = {
	{ MP_ROM_QSTR(MP_QSTR_scale),	MP_ROM_PTR(&amoled_TTF_scale_obj)	 },
	{ MP_ROM_QSTR(MP_QSTR_deinit),  MP_ROM_PTR(&amoled_TTF_deinit_obj)   },
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&amoled_TTF_deinit_obj)   },
};

static MP_DEFINE_CONST_DICT(amoled_TTF_locals_dict, amoled_TTF_locals_dict_table);


#ifdef MP_OBJ_TYPE_GET_SLOT
MP_DEFINE_CONST_OBJ_TYPE(
    amoled_AMOLED_type,
    MP_QSTR_AMOLED,
    MP_TYPE_FLAG_NONE,
    print, amoled_AMOLED_print,
    make_new, amoled_AMOLED_make_new,
    locals_dict, (mp_obj_dict_t *)&amoled_AMOLED_locals_dict
);

MP_DEFINE_CONST_OBJ_TYPE(
    amoled_TTF_type,
    MP_QSTR_TTF,
    MP_TYPE_FLAG_NONE,
    print, amoled_TTF_print,
    make_new, amoled_TTF_make_new,
    locals_dict, (mp_obj_dict_t *)&amoled_TTF_locals_dict
);

#else
	
const mp_obj_type_t amoled_AMOLED_type = {
    { &mp_type_type },
    .name        = MP_QSTR_AMOLED,
    .print       = amoled_AMOLED_print,
    .make_new    = amoled_AMOLED_make_new,
    .locals_dict = (mp_obj_dict_t *)&amoled_AMOLED_locals_dict,
};

const mp_obj_type_t amoled_TTF_type = {
	{ &mp_type_type },
	.name 		= MP_QSTR_TTF,
	.print 		= amoled_TTF_print,
	.make_new	= amoled_TTF_make_new,
	.locals_dic = (mp_obj_dict_t *)&amoled_TTF_locals_dict,
};

#endif


//amoled global library dictionnary

static const mp_map_elem_t mp_module_amoled_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),   MP_OBJ_NEW_QSTR(MP_QSTR_amoled)       },
    { MP_ROM_QSTR(MP_QSTR_AMOLED),     (mp_obj_t)&amoled_AMOLED_type         },
    { MP_ROM_QSTR(MP_QSTR_QSPIPanel),  (mp_obj_t)&amoled_qspi_bus_type       },
    { MP_ROM_QSTR(MP_QSTR_TTF),  	   (mp_obj_t)&amoled_TTF_type       	 },
    { MP_ROM_QSTR(MP_QSTR_RGB),        MP_ROM_INT(COLOR_SPACE_RGB)           },
    { MP_ROM_QSTR(MP_QSTR_BGR),        MP_ROM_INT(COLOR_SPACE_BGR)           },
    { MP_ROM_QSTR(MP_QSTR_MONOCHROME), MP_ROM_INT(COLOR_SPACE_MONOCHROME)    },
    { MP_ROM_QSTR(MP_QSTR_BLACK),      MP_ROM_INT(BLACK)                     },
    { MP_ROM_QSTR(MP_QSTR_BLUE),       MP_ROM_INT(BLUE)                      },
    { MP_ROM_QSTR(MP_QSTR_RED),        MP_ROM_INT(RED)                       },
    { MP_ROM_QSTR(MP_QSTR_GREEN),      MP_ROM_INT(GREEN)                     },
    { MP_ROM_QSTR(MP_QSTR_CYAN),       MP_ROM_INT(CYAN)                      },
    { MP_ROM_QSTR(MP_QSTR_MAGENTA),    MP_ROM_INT(MAGENTA)                   },
    { MP_ROM_QSTR(MP_QSTR_YELLOW),     MP_ROM_INT(YELLOW)                    },
    { MP_ROM_QSTR(MP_QSTR_WHITE),      MP_ROM_INT(WHITE)                     },
};

static MP_DEFINE_CONST_DICT(mp_module_amoled_globals, mp_module_amoled_globals_table);


const mp_obj_module_t mp_module_amoled = {
    .base    = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&mp_module_amoled_globals,
};


#if MICROPY_VERSION >= 0x011300 // MicroPython 1.19 or later
MP_REGISTER_MODULE(MP_QSTR_amoled, mp_module_amoled);
#else
MP_REGISTER_MODULE(MP_QSTR_amoled, mp_module_amoled, MODULE_AMOLED_ENABLE);
#endif

