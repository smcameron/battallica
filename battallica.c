
/* 
    (C) Copyright 2008, Stephen M. Cameron.

    This file is part of battallica.

    battallica is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    battallica is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with battallica; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

 */
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <gtk/gtk.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <gdk/gdkkeysyms.h>


#define SCREEN_WIDTH 800        /* window width, in pixels */
#define SCREEN_HEIGHT 600       /* window height, in pixels */
#define MAXOBJS 8500  		/* max objects in the game */

#define OBJ_TYPE_PLAYER 'p'

/* special values to do with drawing shapes. */
#define LINE_BREAK (-9999)
#define COLOR_CHANGE (-9998) /* note, color change can ONLY follow LINE_BREAK */

struct my_point_t {
	short x,y;
};

#define NPOINTS(x) (sizeof(x) / sizeof(x[0]))

struct my_point_t player_points[] = {
	/* Just a triangle for now. */
	{ 0, -20 },
	{-10, 20 },
	{ 10, 20 },
	{ 0, -20 },
};


/* Just a grouping of arrays of points with the number of points in the array */
struct my_vect_obj {
	int npoints;
	struct my_point_t *p;	
};

/* contains instructions on how to draw all the objects */
struct my_vect_obj player_vect;

#define INIT_VECT(x, y) \
	x.p = y; \
	x.npoints = NPOINTS(y)
	
void init_vects()
{
	INIT_VECT(player_vect, player_points);
}

/*********************************/
/* Game object stuff starts here */

struct game_obj_t;
/* some function pointers which game_obj_t's may have */
typedef void obj_move_func(struct game_obj_t *o);               /* moves and object, called once per frame */
typedef void obj_draw_func(struct game_obj_t *o, GtkWidget *w); /* draws object, called 1/frame, if onscreen */
typedef void obj_destroy_func(struct game_obj_t *o);            /* called when an object is killed */

struct game_obj_t {
	int number; /* offset into go game_object array */
        obj_move_func *move;
        obj_draw_func *draw;
        obj_destroy_func *destroy;
	struct my_vect_obj *v;
        int x, y;                       /* current position, in game coords */
        int vx, vy;                     /* velocity */
	int bearing;
        int color;                      /* initial color */
        int alive;                      /* alive?  Or dead? */
        int otype;                      /* object type */
        // union type_specific_data tsd;   /* the Type Specific Data for this object */
        // struct health_data health;
        struct game_obj_t *next;        /* These pointers, next, prev, are used to construct the */
        struct game_obj_t *prev;        /* target list, the list of things which may be hit by other things */
        int ontargetlist;               /* this list keeps of from having to scan the entire object list. */
};

struct game_obj_t *target_head = NULL;	/* The target list. */


struct game_obj_t *the_player = NULL;
struct game_obj_t *the_enemy = NULL;

int highest_object_number = 0;

/* Game object stuff ends here */
/*******************************/

struct viewport_t {
	int xoffset, yoffset;
	int x, y;
	int vx, vy;
	int width, height;
	struct game_obj_t *obj;
};

struct game_state_t {
	struct viewport_t vp;
	int lives;
	int score;
	struct game_obj_t go[MAXOBJS];
} game_state;

void init_game_state(struct game_obj_t *viewer)
{
	game_state.vp.obj = viewer;
	game_state.vp.x = viewer->x - SCREEN_WIDTH/2;
	game_state.vp.y = viewer->y - SCREEN_HEIGHT/2;
	game_state.vp.vx = viewer->vx;
	game_state.vp.vy = viewer->vy;
	game_state.vp.xoffset = 10;
	game_state.vp.yoffset = 10;
	game_state.vp.width = SCREEN_WIDTH - (game_state.vp.xoffset * 2);
	game_state.vp.height = SCREEN_HEIGHT - (game_state.vp.yoffset * 2);
	game_state.lives = 3;
	game_state.score = 0;
}

typedef void line_drawing_function(GdkDrawable *drawable,
         GdkGC *gc, gint x1, gint y1, gint x2, gint y2);

typedef void bright_line_drawing_function(GdkDrawable *drawable,
         GdkGC *gc, gint x1, gint y1, gint x2, gint y2, int color);

typedef void rectangle_drawing_function(GdkDrawable *drawable,
        GdkGC *gc, gboolean filled, gint x, gint y, gint width, gint height);

typedef void explosion_function(int x, int y, int ivx, int ivy, int v, int nsparks, int time);

line_drawing_function *current_draw_line = gdk_draw_line;
rectangle_drawing_function *current_draw_rectangle = gdk_draw_rectangle;
bright_line_drawing_function *current_bright_line = NULL;
explosion_function *explosion = NULL;

/* I can switch out the line drawing function with these macros */
/* in case I come across something faster than gdk_draw_line */
#define DEFAULT_LINE_STYLE current_draw_line
#define DEFAULT_RECTANGLE_STYLE current_draw_rectangle
#define DEFAULT_BRIGHT_LINE_STYLE current_bright_line

#define wwvi_draw_line DEFAULT_LINE_STYLE
#define wwvi_draw_rectangle DEFAULT_RECTANGLE_STYLE
#define wwvi_bright_line DEFAULT_BRIGHT_LINE_STYLE
int thicklines = 0;
int frame_rate_hz = 30;

GtkWidget *window;
GdkGC *gc = NULL;               /* our graphics context. */
GtkWidget *main_da;             /* main drawing area. */
gint timer_tag;  
int fullscreen = 0;
int in_the_process_of_quitting = 0;

float xscale_screen;
float yscale_screen;
int real_screen_width;
int real_screen_height;

/* cardinal color indexes into huex array */
#define WHITE 0
#define BLUE 1
#define BLACK 2
#define GREEN 3
#define YELLOW 4
#define RED 5
#define ORANGE 6
#define CYAN 7
#define MAGENTA 8
#define DARKGREEN 9

#define NCOLORS 10              /* number of "cardinal" colors */
#define NSPARKCOLORS 25         /* 25 shades from yellow to red for the sparks */
#define NRAINBOWSTEPS (16)
#define NRAINBOWCOLORS (NRAINBOWSTEPS*3)

GdkColor huex[NCOLORS + NSPARKCOLORS + NRAINBOWCOLORS]; /* all the colors we have to work with are in here */

int nframes = 0;
struct timeval start_time, end_time;

void scaled_line(GdkDrawable *drawable,
	GdkGC *gc, gint x1, gint y1, gint x2, gint y2)
{
	gdk_draw_line(drawable, gc, x1*xscale_screen, y1*yscale_screen,
		x2*xscale_screen, y2*yscale_screen);
}

void thick_scaled_line(GdkDrawable *drawable,
	GdkGC *gc, gint x1, gint y1, gint x2, gint y2)
{
	int sx1,sy1,sx2,sy2,dx,dy;

	if (abs(x1-x2) > abs(y1-y2)) {
		dx = 0;
		dy = 1;
	} else {
		dx = 1;
		dy = 0;
	}
	sx1 = x1*xscale_screen;
	sx2 = x2*xscale_screen;
	sy1 = y1*yscale_screen;	
	sy2 = y2*yscale_screen;	
	
	gdk_draw_line(drawable, gc, sx1,sy1,sx2,sy2);
	gdk_draw_line(drawable, gc, sx1-dx,sy1-dy,sx2-dx,sy2-dy);
	gdk_draw_line(drawable, gc, sx1+dx,sy1+dy,sx2+dx,sy2+dy);
}

void scaled_rectangle(GdkDrawable *drawable,
	GdkGC *gc, gboolean filled, gint x, gint y, gint width, gint height)
{
	gdk_draw_rectangle(drawable, gc, filled, x*xscale_screen, y*yscale_screen,
		width*xscale_screen, height*yscale_screen);
}

void scaled_bright_line(GdkDrawable *drawable,
	GdkGC *gc, gint x1, gint y1, gint x2, gint y2, int color)
{
	int sx1,sy1,sx2,sy2,dx,dy;

	if (abs(x1-x2) > abs(y1-y2)) {
		dx = 0;
		dy = 1;
	} else {
		dx = 1;
		dy = 0;
	}
	sx1 = x1*xscale_screen;
	sx2 = x2*xscale_screen;
	sy1 = y1*yscale_screen;	
	sy2 = y2*yscale_screen;	
	
	gdk_gc_set_foreground(gc, &huex[WHITE]);
	gdk_draw_line(drawable, gc, sx1,sy1,sx2,sy2);
	gdk_gc_set_foreground(gc, &huex[color]);
	gdk_draw_line(drawable, gc, sx1-dx,sy1-dy,sx2-dx,sy2-dy);
	gdk_draw_line(drawable, gc, sx1+dx,sy1+dy,sx2+dx,sy2+dy);
}

void unscaled_bright_line(GdkDrawable *drawable,
	GdkGC *gc, gint x1, gint y1, gint x2, gint y2, int color)
{
	int dx,dy;

	if (abs(x1-x2) > abs(y1-y2)) {
		dx = 0;
		dy = 1;
	} else {
		dx = 1;
		dy = 0;
	}
	
	gdk_gc_set_foreground(gc, &huex[WHITE]);
	gdk_draw_line(drawable, gc, x1,y1,x2,y2);
	gdk_gc_set_foreground(gc, &huex[color]);
	gdk_draw_line(drawable, gc, x1-dx,y1-dy,x2-dx,y2-dy);
	gdk_draw_line(drawable, gc, x1+dx,y1+dy,x2+dx,y2+dy);
}

/*******************************************/
/* object allocator code begins            */

/* object allocation algorithm uses a bit per object to indicate */
/* free/allocated... how many 32 bit blocks do we need?  NBITBLOCKS. */
/* 5, 2^5 = 32, 32 bits per int. */

#define NBITBLOCKS ((MAXOBJS >> 5) + 1) 

unsigned int free_obj_bitmap[NBITBLOCKS] = {0}; /* bitmaps for object allocater free/allocated status */

static inline void clearbit(unsigned int *value, unsigned char bit)
{
	*value &= ~(1 << bit);
}

int find_free_obj()
{
	int i, j, answer;
	unsigned int block;

	/* this might be optimized by find_first_zero_bit, or whatever */
	/* it's called that's in the linux kernel.  But, it's pretty */
	/* fast as is, and this is portable without writing asm code. */
	/* Er, portable, except for assuming an int is 32 bits. */

	for (i=0;i<NBITBLOCKS;i++) {
		if (free_obj_bitmap[i] == 0xffffffff) /* is this block full?  continue. */
			continue;

		/* I tried doing a preliminary binary search using bitmasks to figure */
		/* which byte in block contains a free slot so that the for loop only */
		/* compared 8 bits, rather than up to 32.  This resulted in a performance */
		/* drop, (according to the gprof) perhaps contrary to intuition.  My guess */
		/* is branch misprediction hurt performance in that case.  Just a guess though. */

		/* undoubtedly the best way to find the first empty bit in an array of ints */
		/* is some custom ASM code.  But, this is portable, and seems fast enough. */
		/* profile says we spend about 3.8% of time in here. */
	
		/* Not full. There is an empty slot in this block, find it. */
		block = free_obj_bitmap[i];			
		for (j=0;j<32;j++) {
			if (block & 0x01) {	/* is bit j set? */
				block = block >> 1;
				continue;	/* try the next bit. */
			}

			/* Found free bit, bit j.  Set it, marking it non free.  */
			free_obj_bitmap[i] |= (1 << j);
			answer = (i * 32 + j);	/* return the corresponding array index, if in bounds. */
			if (answer < MAXOBJS) {
				if (game_state.go[answer].next != NULL || game_state.go[answer].prev != NULL ||
					game_state.go[answer].ontargetlist) {
						printf("T%c ", game_state.go[answer].otype);
				}
				game_state.go[answer].ontargetlist=0;
				if (answer > highest_object_number)
					highest_object_number = answer;
				return answer;
			}
			return -1;
		}
	}
	return -1;
}

/* object allocator code ends              */
/*******************************************/

/***************************/
/* target list code begins */

/* add an object to the list of targets... */
struct game_obj_t *add_target(struct game_obj_t *o)
{
#ifdef DEBUG_TARGET_LIST
	struct game_obj_t *t;

	for (t = target_head; t != NULL; t = t->next) {
		if (t == o) {
			printf("Object already in target list!\n");
			return NULL;
		}
	}
	if (o->ontargetlist) {
		printf("Object claims to be on target list, but isn't.\n");
	}
#endif

	o->next = target_head;
	o->prev = NULL;
	if (target_head)
		target_head->prev = o;
	target_head = o;
	o->ontargetlist = 1;

	return target_head;
}

/* for debugging... */
void print_target_list()
{
	struct game_obj_t *t;
	printf("Targetlist:\n");
	for (t=target_head; t != NULL;t=t->next) {
		printf("%c: %d,%d\n", t->otype, t->x, t->y);
	}
	printf("end of list.\n");
}

/* remove an object from the target list */
struct game_obj_t *remove_target(struct game_obj_t *t)
{

	struct game_obj_t *next;
	if (!t)
		return NULL;
#ifdef DEBUG_TARGET_LIST
	if (!t->ontargetlist) {
		for (next = target_head; next != NULL; next = next->next) {
			if (next == t) {
				printf("Remove, object claims not to be on target list, but it is.\n");
				goto do_it_anyway;
			}
		}
		return NULL;
	}
do_it_anyway:
#endif
	next = t->next;
	if (t == target_head)
		target_head = t->next;
	if (t->next)
		t->next->prev = t->prev;
	if (t->prev)
		t->prev->next = t->next;
	t->next = NULL;
	t->prev = NULL;
	t->ontargetlist=0;
	return next;
}

/* target list code ends */
/***************************/

/*************************************/
/* random number related code begins */

/* get a random number between 0 and n-1... fast and loose algorithm.  */
static inline int randomn(int n)
{
        /* return (int) (((random() + 0.0) / (RAND_MAX + 0.0)) * (n + 0.0)); */
        /* floating point divide?  No. */
        return ((random() & 0x0000ffff) * n) >> 16;
}

/* get a random number between a and b. */
static inline int randomab(int a, int b)
{
        int n;
        n = abs(a - b);
        return (((random() & 0x0000ffff) * n) >> 16) + min(a,b);
}

/* random number related code ends   */
/*************************************/

void generic_destroy_func(struct game_obj_t *o)
{
	/* so far, nothing needs to be done in this. */
	return;
}

/* this is what can draw a list of line segments with line
 * breaks and color changes...  This gets called quite a lot,
 * so try to make sure it's fast.  There is an inline version
 * of this in draw_objs(), btw. 
 */
void generic_draw(struct game_obj_t *o, GtkWidget *w)
{
	int j;
	int x1, y1, x2, y2;
	
	int vpx, vpy;

	vpx = game_state.vp.x;
	vpy = game_state.vp.y;

	gdk_gc_set_foreground(gc, &huex[o->color]);
	x1 = o->x + o->v->p[0].x - vpx;
	y1 = o->y + o->v->p[0].y - vpy;  
	for (j=0;j<o->v->npoints-1;j++) {
		if (o->v->p[j+1].x == LINE_BREAK) { /* Break in the line segments. */
			j+=2;
			x1 = o->x + o->v->p[j].x - vpx;
			y1 = o->y + o->v->p[j].y - vpy;  
		}
		if (o->v->p[j].x == COLOR_CHANGE) {
			gdk_gc_set_foreground(gc, &huex[o->v->p[j].y]);
			j+=1;
			x1 = o->x + o->v->p[j].x - vpx;
			y1 = o->y + o->v->p[j].y - vpy;  
		}
		x2 = o->x + o->v->p[j+1].x - vpx; 
		y2 = o->y + o->v->p[j+1].y - vpy;
		if (x1 > 0 && x2 > 0)
			wwvi_draw_line(w->window, gc, x1, y1, x2, y2); 
		x1 = x2;
		y1 = y2;
	}
}

void player_move(struct game_obj_t *o)
{
	o->x += o->vx;
	o->y += o->vy;
}


/*****************************/
/* Object adding code begins */

static struct game_obj_t *add_generic_object(int x, int y, int vx, int vy,
	obj_move_func *move_func,
	obj_draw_func *draw_func,
	int color, 
	struct my_vect_obj *vect, 
	int target,  /* can this object be a target? hit by laser, etc? */
	char otype, 
	int alive)
{
	int j;
	struct game_obj_t *o;

	j = find_free_obj();
	if (j < 0)
		return NULL;
	o = &game_state.go[j];
	o->x = x;
	o->y = y;
	o->vx = vx;
	o->vy = vy;
	o->move = move_func;
	o->draw = draw_func;
	o->destroy = generic_destroy_func;
	o->color = color;
	if (target)
		add_target(o);
	else {
		o->prev = NULL;
		o->next = NULL;
	}
	o->v = vect;
	o->otype = otype;
	o->alive = alive;
	return o;
}
/* Object adding code ends */
/*****************************/

/************************************/
/* Terrain related code begins here */
int mapsquarewidth = (SCREEN_HEIGHT / 8);
int mapxdim = 64;
int mapydim = 64;
char *terrain_map = NULL;

struct terrain_descriptor_t {
	char *name;
	char terrain_type;
	int color;
};

struct terrain_descriptor_t grass_terrain = 	{ "grass", '.', GREEN };
struct terrain_descriptor_t mountain_terrain =	{ "mountains", 'm', WHITE };
struct terrain_descriptor_t water_terrain =	{ "water", 'w', CYAN };
struct terrain_descriptor_t forest_terrain =	{ "forest", 'f', GREEN };
struct terrain_descriptor_t swamp_terrain =	{ "swamp", 's', DARKGREEN };

struct terrain_descriptor_t *terrain_type[256];

static inline int txy(int x, int y)
{
	return y*mapydim + x;
}

void init_terrain_types()
{
	int i;
	for (i=0;i<256;i++)
		terrain_type[i] = NULL;

	terrain_type[grass_terrain.terrain_type] = &grass_terrain; 
	terrain_type[mountain_terrain.terrain_type] = &mountain_terrain; 
	terrain_type[water_terrain.terrain_type] = &water_terrain; 
	terrain_type[forest_terrain.terrain_type] = &forest_terrain; 
	terrain_type[swamp_terrain.terrain_type] = &swamp_terrain; 
}

void build_terrain()
{
	int i, x, y;
	char *line;

	terrain_map = (char *) malloc(mapxdim * mapydim);
	memset(terrain_map, grass_terrain.terrain_type, mapxdim*mapydim);

	for (i=0;i<500;i++) {
		x = randomn(64);
		y = randomn(64);
		terrain_map[txy(x,y)] = water_terrain.terrain_type;
	}
	for (i=0;i<500;i++) {
		x = randomn(64);
		y = randomn(64);
		terrain_map[txy(x,y)] = mountain_terrain.terrain_type;
	}
	for (i=0;i<500;i++) {
		x = randomn(64);
		y = randomn(64);
		terrain_map[txy(x,y)] = swamp_terrain.terrain_type;
	}
	for (i=0;i<500;i++) {
		x = randomn(64);
		y = randomn(64);
		terrain_map[txy(x,y)] = forest_terrain.terrain_type;
	}

	line = (char *) malloc(mapxdim + 2);
	for (y = 0; y < mapydim; y++) {
		strncpy(line, &terrain_map[txy(0,y)], mapxdim);
		line[mapxdim] = '\0';
		printf("%s\n", line);
	}
	free(line);
}

/* Terrain related code ends here   */
/************************************/

void init_player()
{
	int i;

	the_player = add_generic_object(
		mapxdim * mapsquarewidth / 2, 
		mapydim * mapsquarewidth / 2,
		0, -4, player_move, generic_draw,
		WHITE, &player_vect, 1, OBJ_TYPE_PLAYER, 1);
}

/**********************************/
/* keyboard handling stuff begins */

struct keyname_value_entry {
	char *name;
	unsigned int value;
} keyname_value_map[] = {
	{ "a", GDK_a }, 
	{ "b", GDK_b }, 
	{ "c", GDK_c }, 
	{ "d", GDK_d }, 
	{ "e", GDK_e }, 
	{ "f", GDK_f }, 
	{ "g", GDK_g }, 
	{ "h", GDK_h }, 
	{ "i", GDK_i }, 
	{ "j", GDK_j }, 
	{ "k", GDK_k }, 
	{ "l", GDK_l }, 
	{ "m", GDK_m }, 
	{ "n", GDK_n }, 
	{ "o", GDK_o }, 
	{ "p", GDK_p }, 
	{ "q", GDK_q }, 
	{ "r", GDK_r }, 
	{ "s", GDK_s }, 
	{ "t", GDK_t }, 
	{ "u", GDK_u }, 
	{ "v", GDK_v }, 
	{ "w", GDK_w }, 
	{ "x", GDK_x }, 
	{ "y", GDK_y }, 
	{ "z", GDK_z }, 
	{ "A", GDK_A }, 
	{ "B", GDK_B }, 
	{ "C", GDK_C }, 
	{ "D", GDK_D }, 
	{ "E", GDK_E }, 
	{ "F", GDK_F }, 
	{ "G", GDK_G }, 
	{ "H", GDK_H }, 
	{ "I", GDK_I }, 
	{ "J", GDK_J }, 
	{ "K", GDK_K }, 
	{ "L", GDK_L }, 
	{ "M", GDK_M }, 
	{ "N", GDK_N }, 
	{ "O", GDK_O }, 
	{ "P", GDK_P }, 
	{ "Q", GDK_Q }, 
	{ "R", GDK_R }, 
	{ "S", GDK_S }, 
	{ "T", GDK_T }, 
	{ "U", GDK_U }, 
	{ "V", GDK_V }, 
	{ "W", GDK_W }, 
	{ "X", GDK_X }, 
	{ "Y", GDK_Y }, 
	{ "Z", GDK_Z }, 
	{ "0", GDK_0 }, 
	{ "1", GDK_1 }, 
	{ "2", GDK_2 }, 
	{ "3", GDK_3 }, 
	{ "4", GDK_4 }, 
	{ "5", GDK_5 }, 
	{ "6", GDK_6 }, 
	{ "7", GDK_7 }, 
	{ "8", GDK_8 }, 
	{ "9", GDK_9 }, 
	{ "-", GDK_minus }, 
	{ "+", GDK_plus }, 
	{ "=", GDK_equal }, 
	{ "?", GDK_question }, 
	{ ".", GDK_period }, 
	{ ",", GDK_comma }, 
	{ "<", GDK_less }, 
	{ ">", GDK_greater }, 
	{ ":", GDK_colon }, 
	{ ";", GDK_semicolon }, 
	{ "@", GDK_at }, 
	{ "*", GDK_asterisk }, 
	{ "$", GDK_dollar }, 
	{ "%", GDK_percent }, 
	{ "&", GDK_ampersand }, 
	{ "'", GDK_apostrophe }, 
	{ "(", GDK_parenleft }, 
	{ ")", GDK_parenright }, 
	{ "space", GDK_space }, 
	{ "enter", GDK_Return }, 
	{ "return", GDK_Return }, 
	{ "backspace", GDK_BackSpace }, 
	{ "delete", GDK_Delete }, 
	{ "pause", GDK_Pause }, 
	{ "scrolllock", GDK_Scroll_Lock }, 
	{ "escape", GDK_Escape }, 
	{ "sysreq", GDK_Sys_Req }, 
	{ "left", GDK_Left }, 
	{ "right", GDK_Right }, 
	{ "up", GDK_Up }, 
	{ "down", GDK_Down }, 
	{ "kp_home", GDK_KP_Home }, 
	{ "kp_down", GDK_KP_Down }, 
	{ "kp_up", GDK_KP_Up }, 
	{ "kp_left", GDK_KP_Left }, 
	{ "kp_right", GDK_KP_Right }, 
	{ "kp_end", GDK_KP_End }, 
	{ "kp_delete", GDK_KP_Delete }, 
	{ "kp_insert", GDK_KP_Insert }, 
	{ "home", GDK_Home }, 
	{ "down", GDK_Down }, 
	{ "up", GDK_Up }, 
	{ "left", GDK_Left }, 
	{ "right", GDK_Right }, 
	{ "end", GDK_End }, 
	{ "delete", GDK_Delete }, 
	{ "insert", GDK_Insert }, 
	{ "kp_0", GDK_KP_0 }, 
	{ "kp_1", GDK_KP_1 }, 
	{ "kp_2", GDK_KP_2 }, 
	{ "kp_3", GDK_KP_3 }, 
	{ "kp_4", GDK_KP_4 }, 
	{ "kp_5", GDK_KP_5 }, 
	{ "kp_6", GDK_KP_6 }, 
	{ "kp_7", GDK_KP_7 }, 
	{ "kp_8", GDK_KP_8 }, 
	{ "kp_9", GDK_KP_9 }, 
	{ "f1", GDK_F1 }, 
	{ "f2", GDK_F2 }, 
	{ "f3", GDK_F3 }, 
	{ "f4", GDK_F4 }, 
	{ "f5", GDK_F5 }, 
	{ "f6", GDK_F6 }, 
	{ "f7", GDK_F7 }, 
	{ "f9", GDK_F9 }, 
	{ "f9", GDK_F9 }, 
	{ "f10", GDK_F10 }, 
	{ "f11", GDK_F11 }, 
	{ "f12", GDK_F12 }, 
};

enum keyaction { keynone, keydown, keyup, keyleft, keyright, 
		keylaser, keytransform, 
		keyquarter, keypause, key2, key3, key4, key5, key6,
		key7, key8, keysuicide, keyfullscreen, keythrust, 
		keysoundeffects, keymusic, keyquit, keytogglemissilealarm,
		keypausehelp, keyreverse
};

enum keyaction keymap[256];
enum keyaction ffkeymap[256];
unsigned char *keycharmap[256];

char *keyactionstring[] = {
	"none", "down", "up", "left", "right", 
	"laser", "bomb", "chaff", "gravitybomb",
	"quarter", "pause", "2x", "3x", "4x", "5x", "6x",
	"7x", "8x", "suicide", "fullscreen", "thrust", 
	"soundeffect", "music", "quit", "missilealarm", "help", "reverse"
};
void init_keymap()
{
	memset(keymap, 0, sizeof(keymap));
	memset(ffkeymap, 0, sizeof(ffkeymap));
	memset(keycharmap, 0, sizeof(keycharmap));

	keymap[GDK_j] = keydown;
	ffkeymap[GDK_Down & 0x00ff] = keydown;

	keymap[GDK_k] = keyup;
	ffkeymap[GDK_Up & 0x00ff] = keyup;

	keymap[GDK_l] = keyright;
	ffkeymap[GDK_Right & 0x00ff] = keyright;
	keymap[GDK_period] = keyright;
	keymap[GDK_greater] = keyright;

	keymap[GDK_h] = keyleft;
	ffkeymap[GDK_Left & 0x00ff] = keyleft;
	keymap[GDK_comma] = keyleft;
	keymap[GDK_less] = keyleft;

	keymap[GDK_space] = keylaser;
	keymap[GDK_z] = keylaser;

	keymap[GDK_b] = keytransform;
	keymap[GDK_x] = keythrust;
	keymap[GDK_p] = keypause;
	ffkeymap[GDK_F1 & 0x00ff] = keypausehelp;
	keymap[GDK_q] = keyquarter;
	keymap[GDK_m] = keymusic;
	keymap[GDK_s] = keysoundeffects;
	ffkeymap[GDK_Escape & 0x00ff] = keyquit;
	keymap[GDK_1] = keytogglemissilealarm;

	keymap[GDK_2] = key2;
	keymap[GDK_3] = key3;
	keymap[GDK_4] = key4;
	keymap[GDK_5] = key5;
	keymap[GDK_6] = key6;
	keymap[GDK_7] = key7;
	keymap[GDK_8] = key8;
	keymap[GDK_9] = keysuicide;

	ffkeymap[GDK_F11 & 0x00ff] = keyfullscreen;
}

static gint key_press_cb(GtkWidget* widget, GdkEventKey* event, gpointer data)
{
	enum keyaction ka;
        if ((event->keyval & 0xff00) == 0) 
                ka = keymap[event->keyval];
        else
                ka = ffkeymap[event->keyval & 0x00ff];

        switch (ka) {
        case keyfullscreen: {
			if (fullscreen) {
				gtk_window_unfullscreen(GTK_WINDOW(window));
				fullscreen = 0;
				/* configure_event takes care of resizing drawing area, etc. */
			} else {
				gtk_window_fullscreen(GTK_WINDOW(window));
				fullscreen = 1;
				/* configure_event takes care of resizing drawing area, etc. */
			}
			return TRUE;
		}
	case keyquit:	in_the_process_of_quitting = !in_the_process_of_quitting;
			break;
	default:
		break;
	}
	return FALSE;
}

static gint key_release_cb(GtkWidget* widget, GdkEventKey* event, gpointer data)
{
	return FALSE;
}

/* keyboard handling stuff ends */
/**********************************/

/* call back for configure_event (for window resize) */
static gint main_da_configure(GtkWidget *w, GdkEventConfigure *event)
{
	GdkRectangle cliprect;

	/* first time through, gc is null, because gc can't be set without */
	/* a window, but, the window isn't there yet until it's shown, but */
	/* the show generates a configure... chicken and egg.  And we can't */
	/* proceed without gc != NULL...  but, it's ok, because 1st time thru */
	/* we already sort of know the drawing area/window size. */
 
	if (gc == NULL)
		return TRUE;

	// gtk_widget_set_size_request(w, w->allocation.width, w->allocation.height);
	// gtk_window_get_size(GTK_WINDOW (w), &real_screen_width, &real_screen_height);
	real_screen_width =  w->allocation.width;
	real_screen_height =  w->allocation.height;
	xscale_screen = (float) real_screen_width / (float) SCREEN_WIDTH;
	yscale_screen = (float) real_screen_height / (float) SCREEN_HEIGHT;
	if (real_screen_width == 800 && real_screen_height == 600) {
		current_draw_line = gdk_draw_line;
		current_draw_rectangle = gdk_draw_rectangle;
		current_bright_line = unscaled_bright_line;
	} else {
		current_draw_line = scaled_line;
		current_draw_rectangle = scaled_rectangle;
		current_bright_line = scaled_bright_line;
		if (thicklines)
			current_draw_line = thick_scaled_line;
	}
	gdk_gc_set_clip_origin(gc, 0, 0);
	cliprect.x = 0;	
	cliprect.y = 0;	
	cliprect.width = real_screen_width;	
	cliprect.height = real_screen_height;	
	gdk_gc_set_clip_rectangle(gc, &cliprect);
	return TRUE;
}

static gboolean delete_event(GtkWidget *widget, 
	GdkEvent *event, gpointer data)
{
    /* If you return FALSE in the "delete_event" signal handler,
     * GTK will emit the "destroy" signal. Returning TRUE means
     * you don't want the window to be destroyed.
     * This is useful for popping up 'are you sure you want to quit?'
     * type dialogs. */

    // g_print ("delete event occurred\n");

    /* Change TRUE to FALSE and the main window will be destroyed with
     * a "delete_event". */
    gettimeofday(&end_time, NULL);
    printf("%d frames / %d seconds, %g frames/sec\n", 
		nframes, (int) (end_time.tv_sec - start_time.tv_sec),
		(0.0 + nframes) / (0.0 + end_time.tv_sec - start_time.tv_sec));
    return FALSE;
}

static void destroy(GtkWidget *widget, gpointer data)
{
    gtk_main_quit ();
}

void really_quit()
{
	gettimeofday(&end_time, NULL);
	printf("%d frames / %d seconds, %g frames/sec\n",
		nframes, (int) (end_time.tv_sec - start_time.tv_sec),
		(0.0 + nframes) / (0.0 + end_time.tv_sec - start_time.tv_sec));
	// destroy_event(window, NULL);
	exit(1); // probably bad form... oh well.
}

static inline int onscreen(struct game_obj_t *o)
{
	return (o->x >= game_state.vp.x && 
		o->x <= game_state.vp.x + game_state.vp.width &&
		o->y >= game_state.vp.y && 
		o->y <= game_state.vp.y + game_state.vp.height);
}

static int generic_draw_terrain(GtkWidget *w, char t, int x, int y)
{
        gdk_gc_set_foreground(gc, &huex[terrain_type[t]->color]);
	wwvi_draw_rectangle(w->window, gc, 0, x+1, y+1, mapsquarewidth-2, mapsquarewidth-2);
	wwvi_draw_line(w->window, gc, x+1, y+1, x+mapsquarewidth-2, y+mapsquarewidth-2);
	wwvi_draw_line(w->window, gc, x+1, y+mapsquarewidth-2, x+mapsquarewidth-2, y+1);
}
 
static int main_da_expose(GtkWidget *w, GdkEvent *event, gpointer p)
{
	int i, tleft, tright, ttop, tbottom, tx, ty, t_x, t_y;
	struct viewport_t *vp = &game_state.vp;

	tleft = game_state.vp.x / mapsquarewidth;
	ttop = game_state.vp.y / mapsquarewidth;
	// printf("left=%d, top=%d\n", tleft, ttop);

	tx = tleft * mapsquarewidth + game_state.vp.x % (mapxdim * mapsquarewidth);
	ty = ttop * mapsquarewidth + game_state.vp.y % (mapydim * mapsquarewidth);
	// printf("tx=%d,ty=%d\n", tx, ty);

	t_y = ttop;
	for (ty = ttop * mapsquarewidth; ty < vp->y + vp->height; ty += mapsquarewidth) {
		t_x = tleft;
		if (t_y < 0) {
			t_y++;
			continue;
		}
		for (tx = tleft * mapsquarewidth; tx < vp->x + vp->width; tx += mapsquarewidth) {
			if (t_x < 0) {
				t_x++;
				continue;
			}
			generic_draw_terrain(w, terrain_map[txy(t_x, t_y)], tx - vp->x, ty - vp->y);
			t_x++;
			if (t_x >= mapydim)
				break;
		}
		t_y++;
		if (t_y >= mapydim)
			break;
	}
	
        gdk_gc_set_foreground(gc, &huex[WHITE]);
	// wwvi_draw_rectangle(w->window, gc, 0, 
	//		vp->xoffset, vp->yoffset, vp->width, vp->height);

	for (i=0;i<=highest_object_number;i++) {
		if (!game_state.go[i].alive)
			continue;
		if (onscreen(&game_state.go[i]))
			game_state.go[i].draw(&game_state.go[i], main_da); 
	}
	return 0;
}

void move_viewport()
{
	struct game_obj_t *v = game_state.vp.obj;
	struct viewport_t *vp = &game_state.vp;
	int desiredx, desiredy;

	if (v->vx > 8)
		desiredx = v->x - SCREEN_WIDTH/4;
	else if (v->vx > 3)
		desiredx = v->x - SCREEN_WIDTH/3;
	else if (v->vx < -3)
		desiredx = v->x - 2*SCREEN_WIDTH/3;
	else if (v->vx < -8)
		desiredx = v->x - 3*SCREEN_WIDTH/4;
	else
		desiredx = v->x - SCREEN_WIDTH/2;

	if (v->vy > 8)
		desiredy = v->y - SCREEN_HEIGHT/4;
	else if (v->vy > 3)
		desiredy = v->y - SCREEN_HEIGHT/3;
	else if (v->vy < -3)
		desiredy = v->y - 2*SCREEN_HEIGHT/3;
	else if (v->vy < -8)
		desiredy = v->y - 3*SCREEN_HEIGHT/4;
	else
		desiredy = v->y - SCREEN_HEIGHT/2;

	if (vp->x < desiredx - 10) {
		if (vp->vx > 0)
			vp->vx = v->vx + 3;
		else
			vp->vx = 3;
	} else if (vp->x < desiredx) {
		if (vp->vx > 0)
			vp->vx = v->vx + 1;
		else
			vp->vx = 1;
	} else if (vp->x > desiredx + 10) {
		if (vp->vx < 0)
			vp->vx = v->vx - 3;
		else 
			vp->vx = -3;
	} else if (vp->x > desiredx) {
		if (vp->vx < 0)
			vp->vx = v->vx - 1;
		else 
			vp->vx = -1;
	} else {
		vp->vx = v->vx;
	}

	if (vp->y < desiredy - 10) {
		if (vp->vy > 0)
			vp->vy = v->vy + 3;
		else
			vp->vy = 3;
	} else if (vp->y < desiredy) {
		if (vp->vy > 0)
			vp->vy = v->vy + 1;
		else
			vp->vy = 1;
	} else if (vp->y > desiredy + 10) {
		if (vp->vy < 0)
			vp->vy = v->vy - 3;
		else 
			vp->vy = -3;
	} else if (vp->y > desiredy) {
		if (vp->vy < 0)
			vp->vy = v->vy - 1;
		else 
			vp->vy = -1;
	} else {
		vp->vy = v->vy;
	}

	vp->x += vp->vx;
	vp->y += vp->vy;
}

gint advance_game(gpointer data)
{
	int i;

	for (i=0;i<=highest_object_number;i++) {
		if (!game_state.go[i].alive)
			continue;
		game_state.go[i].move(&game_state.go[i]);
	}
	move_viewport();
	
	gdk_threads_enter();
	gtk_widget_queue_draw(main_da);
	nframes++;
	gdk_threads_leave();
	if (in_the_process_of_quitting)
		really_quit();
	return TRUE;
}

int main(int argc, char *argv[])
{
	GtkWidget *vbox;
	int i;

	real_screen_width = SCREEN_WIDTH;
	real_screen_height = SCREEN_HEIGHT;

	gtk_set_locale();
	gtk_init (&argc, &argv);

	init_keymap();
	init_terrain_types();
	init_vects();
	init_player();
	init_game_state(the_player);

	build_terrain();

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);		
	gtk_container_set_border_width (GTK_CONTAINER (window), 0);
	vbox = gtk_vbox_new(FALSE, 0);
        main_da = gtk_drawing_area_new();

	g_signal_connect (G_OBJECT (window), "delete_event",
		G_CALLBACK (delete_event), NULL);
	g_signal_connect (G_OBJECT (window), "destroy",
		G_CALLBACK (destroy), NULL);
	g_signal_connect(G_OBJECT (window), "key_press_event",
		G_CALLBACK (key_press_cb), "window");
	g_signal_connect(G_OBJECT (window), "key_release_event",
		G_CALLBACK (key_release_cb), "window");
	g_signal_connect(G_OBJECT (main_da), "expose_event",
		G_CALLBACK (main_da_expose), NULL);
        g_signal_connect(G_OBJECT (main_da), "configure_event",
		G_CALLBACK (main_da_configure), NULL);

	gdk_color_parse("white", &huex[WHITE]);
	gdk_color_parse("blue", &huex[BLUE]);
	gdk_color_parse("black", &huex[BLACK]);
	gdk_color_parse("green", &huex[GREEN]);
	gdk_color_parse("darkgreen", &huex[DARKGREEN]);
	gdk_color_parse("yellow", &huex[YELLOW]);
	gdk_color_parse("red", &huex[RED]);
	gdk_color_parse("orange", &huex[ORANGE]);
	gdk_color_parse("cyan", &huex[CYAN]);
	gdk_color_parse("MAGENTA", &huex[MAGENTA]);

	gtk_container_add (GTK_CONTAINER (window), vbox);
	gtk_box_pack_start(GTK_BOX (vbox), main_da, TRUE /* expand */, TRUE /* fill */, 0);

	gtk_widget_modify_bg(main_da, GTK_STATE_NORMAL, &huex[BLACK]);

        gtk_window_set_default_size(GTK_WINDOW(window), real_screen_width, real_screen_height);

        gtk_widget_show (vbox);
        gtk_widget_show (main_da);
        gtk_widget_show (window);

	for (i=0;i<NCOLORS+NSPARKCOLORS + NRAINBOWCOLORS;i++)
		gdk_colormap_alloc_color(gtk_widget_get_colormap(main_da), &huex[i], FALSE, FALSE);
        gc = gdk_gc_new(GTK_WIDGET(main_da)->window);
        gdk_gc_set_foreground(gc, &huex[BLUE]);
        gdk_gc_set_foreground(gc, &huex[WHITE]);

	timer_tag = g_timeout_add(1000 / frame_rate_hz, advance_game, NULL);

	/* Apparently (some versions of?) portaudio calls g_thread_init(). */
	/* It may only be called once, and subsequent calls abort, so */
	/* only call it if the thread system is not already initialized. */
	if (!g_thread_supported ())
		g_thread_init(NULL);
	gdk_threads_init();

	gettimeofday(&start_time, NULL);

	gtk_main ();
	return 0;
}
