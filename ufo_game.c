/*
 * tiles.c
 * program which demonstraes tile mode 0
 */

/* include the image we are using */
#include "space.h"

/* include the tile map we are using */
#include "space_map.h"

#include "ufo.h"

/* the width and height of the screen */
#define WIDTH 240
#define HEIGHT 160

/* the three tile modes */
#define MODE0 0x00
#define MODE1 0x01
#define MODE2 0x02

/* enable bits for the four tile layers */
#define BG0_ENABLE 0x100
#define BG1_ENABLE 0x200
#define BG2_ENABLE 0x400
#define BG3_ENABLE 0x800

/* the address of the color palettes used for backgrounds and sprites */
volatile unsigned short* sprite_palette = (volatile unsigned short*) 0x5000200;

/* the memory location which stores sprite image data */
volatile unsigned short* sprite_image_memory = (volatile unsigned short*) 0x6010000;

volatile unsigned short* sprite_attribute_memory = (volatile unsigned short*) 0x7000000;


/* the control registers for the four tile layers */
volatile unsigned short* bg0_control = (volatile unsigned short*) 0x4000008;
volatile unsigned short* bg1_control = (volatile unsigned short*) 0x400000a;
volatile unsigned short* bg2_control = (volatile unsigned short*) 0x400000c;
volatile unsigned short* bg3_control = (volatile unsigned short*) 0x400000e;

/* palette is always 256 colors */
#define PALETTE_SIZE 256
#define NUM_SPRITES 128
/* flags to set sprite handling in display control register */
#define SPRITE_MAP_2D 0x0
#define SPRITE_MAP_1D 0x40
#define SPRITE_ENABLE 0x1000
/* the display control pointer points to the gba graphics register */
volatile unsigned long* display_control = (volatile unsigned long*) 0x4000000;

/* the address of the color palette */
volatile unsigned short* bg_palette = (volatile unsigned short*) 0x5000000;

/* the button register holds the bits which indicate whether each button has
 * been pressed - this has got to be volatile as well
 */
volatile unsigned short* buttons = (volatile unsigned short*) 0x04000130;

/* scrolling registers for backgrounds */
volatile short* bg0_x_scroll = (unsigned short*) 0x4000010;
volatile short* bg0_y_scroll = (unsigned short*) 0x4000012;
volatile short* bg1_x_scroll = (unsigned short*) 0x4000014;
volatile short* bg1_y_scroll = (unsigned short*) 0x4000016;
volatile short* bg2_x_scroll = (unsigned short*) 0x4000018;
volatile short* bg2_y_scroll = (unsigned short*) 0x400001a;
volatile short* bg3_x_scroll = (unsigned short*) 0x400001c;
volatile short* bg3_y_scroll = (unsigned short*) 0x400001e;


/* the bit positions indicate each button - the first bit is for A, second for
 * B, and so on, each constant below can be ANDED into the register to get the
 * status of any one button */
#define BUTTON_A (1 << 0)
#define BUTTON_B (1 << 1)
#define BUTTON_SELECT (1 << 2)
#define BUTTON_START (1 << 3)
#define BUTTON_RIGHT (1 << 4)
#define BUTTON_LEFT (1 << 5)
#define BUTTON_UP (1 << 6)
#define BUTTON_DOWN (1 << 7)
#define BUTTON_R (1 << 8)
#define BUTTON_L (1 << 9)

/* the scanline counter is a memory cell which is updated to indicate how
 * much of the screen has been drawn */
volatile unsigned short* scanline_counter = (volatile unsigned short*) 0x4000006;

#define DMA_ENABLE 0x80000000

/* flags for the sizes to transfer, 16 or 32 bits */
#define DMA_16 0x00000000
#define DMA_32 0x04000000

/* pointer to the DMA source location */
volatile unsigned int* dma_source = (volatile unsigned int*) 0x40000D4;

/* pointer to the DMA destination location */
volatile unsigned int* dma_destination = (volatile unsigned int*) 0x40000D8;

/* pointer to the DMA count/control */
volatile unsigned int* dma_count = (volatile unsigned int*) 0x40000DC;

/* copy data using DMA */
void memcpy16_dma(unsigned short* dest, unsigned short* source, int amount) {
    *dma_source = (unsigned int) source;
    *dma_destination = (unsigned int) dest;
    *dma_count = amount | DMA_16 | DMA_ENABLE;
}

/* setup the sprite image and palette */
void setup_sprite_image() {
    /* load the palette from the image into palette memory*/
    memcpy16_dma((unsigned short*) sprite_palette, (unsigned short*) ufo_palette, PALETTE_SIZE);

    /* load the image into sprite image memory */
    memcpy16_dma((unsigned short*) sprite_image_memory, (unsigned short*) ufo_data, (ufo_width * ufo_height) / 2);
	
}
/* a sprite is a moveable image on the screen */
struct Sprite {
    unsigned short attribute0;
    unsigned short attribute1;
    unsigned short attribute2;
    unsigned short attribute3;
};

struct Sprite sprites[NUM_SPRITES];
int next_sprite_index = 0;

/* the different sizes of sprites which are possible */
enum SpriteSize {
    SIZE_8_8,
    SIZE_16_16,
    SIZE_32_32,
    SIZE_64_64,
    SIZE_16_8,
    SIZE_32_8,
    SIZE_32_16,
    SIZE_64_32,
    SIZE_8_16,
    SIZE_8_32,
    SIZE_16_32,
    SIZE_32_64
};
struct Sprite* sprite_init(int x, int y, enum SpriteSize size,
         int tile_index, int priority) {

    /* grab the next index */
    int index = next_sprite_index++;

    /* setup the bits used for each shape/size possible */
    int size_bits, shape_bits;
    switch (size) {
        case SIZE_8_8:   size_bits = 0; shape_bits = 0; break;
        case SIZE_16_16: size_bits = 1; shape_bits = 0; break;
        case SIZE_32_32: size_bits = 2; shape_bits = 0; break;
        case SIZE_64_64: size_bits = 3; shape_bits = 0; break;
        case SIZE_16_8:  size_bits = 0; shape_bits = 1; break;
        case SIZE_32_8:  size_bits = 1; shape_bits = 1; break;
        case SIZE_32_16: size_bits = 2; shape_bits = 1; break;
        case SIZE_64_32: size_bits = 3; shape_bits = 1; break;
        case SIZE_8_16:  size_bits = 0; shape_bits = 2; break;
        case SIZE_8_32:  size_bits = 1; shape_bits = 2; break;
        case SIZE_16_32: size_bits = 2; shape_bits = 2; break;
        case SIZE_32_64: size_bits = 3; shape_bits = 2; break;
    }

    /* set up the first attribute */
    sprites[index].attribute0 = y     |         /* y coordinate */
                            (0 << 8)  |         /* rendering mode */
                            (0 << 10) |         /* gfx mode */
                            (0 << 12) |         /* mosaic */
                            (1 << 13) |         /* color mode, 0:16, 1:256 */
                            (shape_bits << 14); /* shape */

    /* set up the second attribute */
    sprites[index].attribute1 = x     |         /* x coordinate */
                            (0 << 9)  |         /* affine flag */
                            (0 << 12) |         /* horizontal flip flag */
                            (0 << 13) |         /* vertical flip flag */
                            (size_bits << 14);  /* size */

    /* setup the second attribute */
    sprites[index].attribute2 = tile_index   |  /* tile index */
                            (priority << 10) |  /* priority */
                            (0 << 12);          /* palette bank (only 16 color)*/

    /* return pointer to this sprite */
    return &sprites[index];
}

/* set a sprite position */
void sprite_position(struct Sprite* sprite, int x, int y) {
    /* clear out the y coordinate */
    sprite->attribute0 &= 0xff00;

    /* set the new y coordinate */
    sprite->attribute0 |= (y & 0xff);

    /* clear out the x coordinate */
    sprite->attribute1 &= 0xfe00;

    /* set the new x coordinate */
    sprite->attribute1 |= (x & 0x1ff);
}
/* move a sprite in a direction */
void sprite_move(struct Sprite* sprite, int dx, int dy) {
    /* get the current y coordinate */
    int y = sprite->attribute0 & 0xff;

    /* get the current x coordinate */
    int x = sprite->attribute1 & 0x1ff;

    /* move to the new location */
    sprite_position(sprite, x + dx, y + dy);
}
/* change the tile offset of a sprite */
void sprite_set_offset(struct Sprite* sprite, int offset) {
    /* clear the old offset */
    sprite->attribute2 &= 0xfc00;

    /* apply the new one */
    sprite->attribute2 |= (offset & 0x03ff);
}
/* update all of the spries on the screen */
void sprite_update_all() {
    /* copy them all over */
    memcpy16_dma((unsigned short*) sprite_attribute_memory, (unsigned short*) sprites, NUM_SPRITES * 4);
}
struct UFO {
    /* the actual sprite attribute info */
    struct Sprite* sprite;

    /* the x and y position */
    int x, y;

    /* which frame of the animation he is on */
    int frame;

    /* the number of frames to wait before flipping */
    int animation_delay;

    /* the animation counter counts how many frames until we flip */
    int counter;
};
struct Explosion {
    /* the actual sprite attribute info */
    struct Sprite* sprite;

    /* the x and y position */
    int x, y;

    /* which frame of the animation he is on */
    int frame;

    /* the number of frames to wait before flipping */
    int animation_delay;

    /* the animation counter counts how many frames until we flip */
    int counter;
};

unsigned short tile_lookup(int x, int y, int xscroll, int yscroll,
        const unsigned short* tilemap, int tilemap_w, int tilemap_h) {

    /* adjust for the scroll */
    x += xscroll;
    y += yscroll;

    /* convert from screen coordinates to tile coordinates */
    x >>= 3;
    y >>= 3;

    /* account for wraparound */
    while (x >= tilemap_w) {
        x -= tilemap_w;
    }
    while (y >= tilemap_h) {
        y -= tilemap_h;
    }
    while (x < 0) {
        x += tilemap_w;
    }
    while (y < 0) {
        y += tilemap_h;
    }

    /* the larger screen maps (bigger than 32x32) are made of multiple s  titched
     * together - the offset is used for finding which screen block we a  re in
     * for these cases */
    int offset = 0;

    /* if the width is 64, add 0x400 offset to get to tile maps on right   */
    if (tilemap_w == 64 && x >= 32) {
        x -= 32;
        offset += 0x400;
    }

    /* if height is 64 and were down there */
    if (tilemap_h == 64 && y >= 32) {
        y -= 32;

        /* if width is also 64 add 0x800, else just 0x400 */
        if (tilemap_w == 64) {
            offset += 0x800;
        } else {
            offset += 0x400;
        }
    }

    /* find the index in this tile map */
    int index = y * 32 + x;

    /* return the tile */
    return tilemap[index + offset];
}
/* initialize the ufo */
void ufo_init(struct UFO* ufo) {
    ufo->x = 112;
    ufo->y = 72;
    ufo->frame = 64;
    ufo->counter = 0;
    ufo->animation_delay = 6;
    ufo->sprite = sprite_init(ufo->x, ufo->y, SIZE_32_16, ufo->frame, 1);
}
void explo_init(struct Explosion* explo) {
    explo->x = 241;
    explo->y = 0;
    explo->frame = 0;
    explo->counter = 0;
    explo->animation_delay = 4;
    explo->sprite = sprite_init(explo->x, explo->y, SIZE_32_32, explo->frame, 0);
}
void explo_update(struct Explosion* explo, int position) { 
	explo->counter++;
	if (explo->counter >= explo->animation_delay) {
        explo->frame = explo->frame + 32;
        if (explo->frame > 32) {
            explo->frame = 0;
        }
        sprite_set_offset(explo->sprite, explo->frame);
        explo->counter = 0;
    }
	if (position==1) {
    sprite_position(explo->sprite, 104, 64);
	}
	else {
	sprite_position(explo->sprite, 241, 0);
	}
}
/* update the ufo */
int ufo_update(struct UFO* ufo, int xscroll, int yscroll) {
	unsigned short top_collider = tile_lookup(ufo->x + 8, ufo->y, xscroll, yscroll, space_map, space_map_width, space_map_height);
	 
	unsigned short bottom_collider = tile_lookup(ufo->x + 8, ufo->y + 15, xscroll, yscroll, space_map, space_map_width, space_map_height);
	 
	unsigned short right_collider = tile_lookup(ufo->x + 15, ufo->y + 8, xscroll, yscroll, space_map, space_map_width, space_map_height);
	 
	unsigned short left_collider = tile_lookup(ufo->x, ufo->y + 8, xscroll, yscroll, space_map, space_map_width, space_map_height);
	 
    ufo->counter++;
	if (ufo->counter >= ufo->animation_delay) {
        ufo->frame = ufo->frame + 16;
        if (ufo->frame > 96) {
            ufo->frame = 64;
        }
        sprite_set_offset(ufo->sprite, ufo->frame);
        ufo->counter = 0;
    }

    sprite_position(ufo->sprite, ufo->x, ufo->y);
	
	if (top_collider == 2) {
		return 1;
	}
	else if (bottom_collider == 2) {
		return 1;
	}
	else if (right_collider == 2) {
		return 1;
	}
	else if (left_collider == 2) {
		return 1;
	}
	else {
		return 0;
	}
}

/* wait for the screen to be fully drawn so we can do something during vblank */
void wait_vblank() {
    /* wait until all 160 lines have been updated */
    while (*scanline_counter < 160) { }
}
int death_count(int a);
int speed(int a);


/* this function checks whether a particular button has been pressed */
unsigned char button_pressed(unsigned short button) {
    /* and the button register with the button constant we want */
    unsigned short pressed = *buttons & button;

    /* if this value is zero, then it's not pressed */
    if (pressed == 0) {
        return 1;
    } else {
        return 0;
    }
}


/* return a pointer to one of the 4 character blocks (0-3) */
volatile unsigned short* char_block(unsigned long block) {
    /* they are each 16K big */
    return (volatile unsigned short*) (0x6000000 + (block * 0x4000));
}

/* return a pointer to one of the 32 screen blocks (0-31) */
volatile unsigned short* screen_block(unsigned long block) {
    /* they are each 2K big */
    return (volatile unsigned short*) (0x6000000 + (block * 0x800));
}


/* function to setup background 0 for this program */
void setup_background() {

    /* load the palette from the image into palette memory*/
    for (int i = 0; i < PALETTE_SIZE; i++) {
        bg_palette[i] = space_palette[i];
    }

    /* load the image into char block 0 (16 bits at a time) */
    volatile unsigned short* dest = char_block(0);
    unsigned short* image = (unsigned short*) space_data;
    for (int i = 0; i < ((space_width * space_height) / 2); i++) {
        dest[i] = image[i];
    }

    /* set all control the bits in this register */
    *bg0_control = 1 |    /* priority, 0 is highest, 3 is lowest */
        (0 << 2)  |       /* the char block the image data is stored in */
        (0 << 6)  |       /* the mosaic flag */
        (1 << 7)  |       /* color mode, 0 is 16 colors, 1 is 256 colors */
        (16 << 8) |       /* the screen block the tile data is stored in */
        (1 << 13) |       /* wrapping flag */
        (3 << 14);        /* bg size, 0 is 256x256 */

    /* load the tile data into screen block 16 */
    dest = screen_block(16);
    for (int i = 0; i < (space_map_width * space_map_height); i++) {
        dest[i] = space_map[i];
    }
}
/* setup all sprites */
void sprite_clear() {
    /* clear the index counter */
    next_sprite_index = 0;

    /* move all sprites offscreen to hide them */
    for(int i = 0; i < NUM_SPRITES; i++) {
        sprites[i].attribute0 = HEIGHT;
        sprites[i].attribute1 = WIDTH;
    }
}
/* finds which tile a screen coordinate maps to, taking scroll into acco  unt */



/* just kill time */
void delay(unsigned int amount) {
    for (int i = 0; i < amount * 10; i++);
}

/* the main function */
int main() {
    /* we set the mode to mode 0 with bg0 on */
	*display_control = MODE0 | BG0_ENABLE | SPRITE_ENABLE | SPRITE_MAP_1D;

    /* setup the background 0 */
    setup_background();
	
	setup_sprite_image();
	
	sprite_clear();
	
	struct UFO ufo;
	ufo_init(&ufo);
	
	struct Explosion explo;
	explo_init(&explo);

    /* set initial scroll to 0 */
    int xscroll = 48;
    int yscroll = 0;
	
	//set initial speed velocity counters to 0
	int xcount = 0;
	int ycount = 0;
	int collide = 0;
	int explode_count = 0;
	int load = 0;

    /* loop forever */
    while (1) {
		collide = ufo_update(&ufo, xscroll, yscroll);
		/* scroll with the arrow keys */
		if (collide != 1) {
			if (button_pressed(BUTTON_DOWN)) {
				if (ycount < 40) {
					ycount++;
				}
			}
			if (button_pressed(BUTTON_UP)) {
				if (ycount > -40) {
					ycount--;
				}
			}
			if (button_pressed(BUTTON_RIGHT)) {
				if (xcount < 40) {
					xcount++;
				}
			}
			if (button_pressed(BUTTON_LEFT)) {
				if (xcount > -40) {
					xcount--;
				}
			}
		
			xscroll+= (int) xcount/10;
			yscroll+= (int) ycount/10;
		}
		if (collide == 1 && load == 1) {
			explode_count++;
			explo_update(&explo,1);
			if (explode_count > 40) {
				explode_count=0;
				xscroll = 48;
				yscroll = 0;
				xcount = 0;
				ycount = 0;
				collide = 0;
				explo_update(&explo,0);
			}
		} 
		if (load == 0) {
			load = 1;
		}
		/* wait for vblank before scrolling */
		wait_vblank();
		
		*bg0_x_scroll = xscroll;
		*bg0_y_scroll = yscroll;
		
		sprite_update_all();

		/* delay a lot */
		delay(1000);
    }
}

