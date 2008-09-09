
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

#define SCREEN_WIDTH 800        /* window width, in pixels */
#define SCREEN_HEIGHT 600       /* window height, in pixels */

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

static int main_da_expose(GtkWidget *w, GdkEvent *event, gpointer p)
{
	wwvi_draw_line(w->window, gc, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
	return 0;
}

static gint key_press_cb(GtkWidget* widget, GdkEventKey* event, gpointer data)
{
	return FALSE;
}

static gint key_release_cb(GtkWidget* widget, GdkEventKey* event, gpointer data)
{
	return FALSE;
}

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

gint advance_game(gpointer data)
{
	gdk_threads_enter();
	gtk_widget_queue_draw(main_da);
	nframes++;
	gdk_threads_leave();
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