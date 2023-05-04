//
// program: intersectionSimulation.c
// author:  Braden Stitt
// purpose: Framework for simple graphics
//         Using XWindows (Xlib) for drawing pixels
//         Double buffer used for smooth animation
//
// DBE means double buffer extension.
// It's an extersion of X11 that uses a video back-buffer.
// Draw to a back-buffer, then swap it to the video memory when ready.
//
// Press 'C' to see collisions of the cars in the intersection.
// Press 'S' to slow the cars in the intersection.
// The position of the collision is marked.
// Stop the collisions using POSIX semaphores.
//
//
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/Xdbe.h>
#include <semaphore.h>
sem_t sem;

void init();
void init_xwindows(int, int);
void cleanup_xwindows(void);
void check_resize(XEvent *e);
void check_mouse(XEvent *e);
int check_keys(XEvent *e);
void physics(void);
void render(void);
//
//---------------------------------
// globals
struct Global
{
	Display *dpy;
	Window win;
	GC gc;
	XdbeBackBuffer backBuffer;
	XdbeSwapInfo swapInfo;
	int xres, yres;
	int collision_flag;
	int collision[4];
	int crash[2];
	int show_collisions;
	int ncollisions;
	int slow_mode;
} g;

struct Box
{
	double pos[2];
	double vel[2];
	int w, h;
} intersection, cars[4];

int main(void)
{
	init();
	init_xwindows(400, 400);
	//
	pthread_t tid[4];
	void *traffic(void *arg);
	pthread_create(&tid[0], NULL, traffic, (void *)(long)0);
	pthread_create(&tid[1], NULL, traffic, (void *)(long)1);
	pthread_create(&tid[2], NULL, traffic, (void *)(long)2);
	pthread_create(&tid[3], NULL, traffic, (void *)(long)3);
	//
	int done = 0;
	while (!done)
	{
		// Handle all events in queue...
		while (XPending(g.dpy))
		{
			XEvent e;
			XNextEvent(g.dpy, &e);
			check_resize(&e);
			check_mouse(&e);
			done = check_keys(&e);
		}
		// Process physics and rendering every frame
		physics();
		render();
		XdbeSwapBuffers(g.dpy, &g.swapInfo, 1);
		usleep(4000);
	}
	cleanup_xwindows();
	return 0;
}

int fib(int n)
{
	if (n == 1 || n == 2)
		return 1;
	return fib(n - 1) + fib(n - 2);
}

int overlap(struct Box *c, struct Box *i)
{
	// Does one rectangle overlap another rectangle?
	if (c->pos[0] + (c->w >> 1) < i->pos[0] - (i->w >> 1))
		return 0;
	if (c->pos[0] - (c->w >> 1) > i->pos[0] + (i->w >> 1))
		return 0;
	if (c->pos[1] + (c->h >> 1) < i->pos[1] - (i->h >> 1))
		return 0;
	if (c->pos[1] - (c->h >> 1) > i->pos[1] + (i->h >> 1))
		return 0;
	return 1;
}

void *traffic(void *arg)
{
	int carnum = (int)(long)arg;
	// This thread will run forever.
	// Calls to fib() are used to slow down.
	while (1)
	{
		fib(rand() % 5 + 2);
		// move the car...
		cars[carnum].pos[0] += cars[carnum].vel[0];
		cars[carnum].pos[1] += cars[carnum].vel[1];
		// Is car in the intersection???
		if (overlap(&cars[carnum], &intersection))
		{
			sem_wait(&sem)
				// Car is in the intersection.
				while (overlap(&cars[carnum], &intersection))
			{
				// Loop here until out of the intersection.
				fib(rand() % 5 + 2);
				if (g.slow_mode)
					fib(19);
				// move the car...
				cars[carnum].pos[0] += cars[carnum].vel[0];
				cars[carnum].pos[1] += cars[carnum].vel[1];
			}
			// Car is out of the intersection.
			sem_post(&sem);
		}
		// Is car outside of the window???
		// Car will enter from other side of window.
		// left
		if (cars[carnum].pos[0] < -20 && cars[carnum].vel[0] < 0.0)
		{
			cars[carnum].pos[0] += g.xres + 40.0;
			cars[carnum].vel[0] = -(rand() % 3 + 1);
			cars[carnum].vel[0] *= 0.0002;
		}
		// top
		if (cars[carnum].pos[1] < -20 && cars[carnum].vel[1] < 0.0)
		{
			cars[carnum].pos[1] += g.yres + 40.0;
			cars[carnum].vel[1] = -(rand() % 3 + 1);
			cars[carnum].vel[1] *= 0.0002;
		}
		// right
		if (cars[carnum].pos[0] > g.xres + 20 && cars[carnum].vel[0] > 0.0)
		{
			cars[carnum].pos[0] -= (g.xres + 40.0);
			cars[carnum].vel[0] = (rand() % 3 + 1);
			cars[carnum].vel[0] *= 0.0002;
		}
		// bottom
		if (cars[carnum].pos[1] > g.yres + 20 && cars[carnum].vel[1] > 0.0)
		{
			cars[carnum].pos[1] -= (g.yres + 40.0);
			cars[carnum].vel[1] = (rand() % 3 + 1);
			cars[carnum].vel[1] *= 0.0002;
		}
	}
	return (void *)0;
}

void cleanup_xwindows(void)
{
	// Deallocate back buffer
	if (!XdbeDeallocateBackBufferName(g.dpy, g.backBuffer))
	{
		fprintf(stderr, "Error : unable to deallocate back buffer.\n");
	}
	XFreeGC(g.dpy, g.gc);
	XDestroyWindow(g.dpy, g.win);
	XCloseDisplay(g.dpy);
}

void set_window_title()
{
	char ts[256];
	sprintf(ts, "CMPS-3600 lab-14 %ix%i", g.xres, g.yres);
	XStoreName(g.dpy, g.win, ts);
}

void init_xwindows(int w, int h)
{
	g.xres = w;
	g.yres = h;
	XSetWindowAttributes attributes;
	// int screen;
	int major, minor;
	XdbeBackBufferAttributes *backAttr;
	// XGCValues gcv;
	g.dpy = XOpenDisplay(NULL);
	// Use default screen
	// screen = DefaultScreen(dpy);
	// List of events we want to handle
	attributes.event_mask = ExposureMask | StructureNotifyMask |
							PointerMotionMask | ButtonPressMask |
							ButtonReleaseMask | KeyPressMask | KeyReleaseMask;
	// Various window attributes
	attributes.backing_store = Always;
	attributes.save_under = True;
	attributes.override_redirect = False;
	attributes.background_pixel = 0x00000000;
	// Get default root window
	Window root;
	root = DefaultRootWindow(g.dpy);
	// Create a window
	g.win = XCreateWindow(g.dpy, root, 0, 0, g.xres, g.yres, 0,
						  CopyFromParent, InputOutput, CopyFromParent,
						  CWBackingStore | CWOverrideRedirect | CWEventMask |
							  CWSaveUnder | CWBackPixel,
						  &attributes);
	// Create gc
	g.gc = XCreateGC(g.dpy, g.win, 0, NULL);
	// Get DBE version
	if (!XdbeQueryExtension(g.dpy, &major, &minor))
	{
		fprintf(stderr, "Error : unable to fetch Xdbe Version.\n");
		XFreeGC(g.dpy, g.gc);
		XDestroyWindow(g.dpy, g.win);
		XCloseDisplay(g.dpy);
		exit(1);
	}
	printf("Xdbe version %d.%d\n", major, minor);
	// Get back buffer and attributes (used for swapping)
	g.backBuffer = XdbeAllocateBackBufferName(g.dpy, g.win, XdbeUndefined);
	backAttr = XdbeGetBackBufferAttributes(g.dpy, g.backBuffer);
	g.swapInfo.swap_window = backAttr->window;
	g.swapInfo.swap_action = XdbeUndefined;
	XFree(backAttr);
	// Map and raise window
	set_window_title();
	XMapWindow(g.dpy, g.win);
	XRaiseWindow(g.dpy, g.win);
	//
}

void fillRectangle(int x, int y, int w, int h)
{
	XFillRectangle(g.dpy, g.backBuffer, g.gc, x, y, w, h);
}

void drawRectangle(int x, int y, int w, int h)
{
	XDrawRectangle(g.dpy, g.backBuffer, g.gc, x, y, w, h);
}

void drawLine(int x0, int y0, int x1, int y1)
{
	XDrawLine(g.dpy, g.backBuffer, g.gc, x0, y0, x1, y1);
}

void drawString(int x, int y, char *str)
{
	XDrawString(g.dpy, g.backBuffer, g.gc, x, y, str, strlen(str));
}

void init(void)
{
	// Initialize cars direction, speed, etc.
	srand((unsigned)time(NULL));
	g.collision_flag = 0;
	g.show_collisions = 0;
	g.ncollisions = 0;
	g.slow_mode = 0;
	// the intersection
	intersection.w = 100;
	intersection.h = 100;
	intersection.pos[0] = g.xres / 2;
	intersection.pos[1] = g.yres / 2;
	intersection.vel[0] = 0;
	intersection.vel[1] = 0;
	//
	// cars...
	//
	//         1
	//         |
	//         v
	//       +-----+
	//       |     | <--2
	//  0--> |     |
	//       +-----+
	//           ^
	//           |
	//           3
	//
	int i;
	for (i = 0; i < 4; i++)
	{
		cars[i].w = 18;
		cars[i].h = 18;
		cars[i].pos[0] = intersection.pos[0];
		cars[i].pos[1] = intersection.pos[1];
		cars[i].vel[0] = 0;
		cars[i].vel[1] = 0;
	}
	int offset = 21;
	// Car heading West
	i = 0;
	cars[i].w += rand() % 6 + 10;
	cars[i].pos[0] = g.xres + 30;
	cars[i].pos[1] -= offset;
	cars[i].vel[0] = -(rand() % 3 + 1);
	cars[i].vel[1] = 0;
	// Car heading South
	i = 1;
	cars[i].h += rand() % 6 + 10;
	cars[i].pos[0] -= offset;
	cars[i].pos[1] = -30;
	cars[i].vel[0] = 0;
	cars[i].vel[1] = rand() % 3 + 1;
	// Car heading East
	i = 2;
	cars[i].w += rand() % 6 + 10;
	cars[i].pos[0] = -40;
	cars[i].pos[1] += offset;
	cars[i].vel[0] = rand() % 3 + 1;
	cars[i].vel[1] = 0;
	// Car heading North
	i = 3;
	cars[i].h += rand() % 6 + 10;
	cars[i].pos[0] += offset;
	// printf("offset: %i\n", offset);
	// printf("car[%i].pos[0]: %lf\n", i, cars[i].pos[0]);
	cars[i].pos[1] = g.yres + 30;
	cars[i].vel[0] = 0;
	cars[i].vel[1] = -(rand() % 3 + 1);
	// Scale the velocity...
	for (i = 0; i < 4; i++)
	{
		cars[i].vel[0] *= 0.0002;
		cars[i].vel[1] *= 0.0002;
	}
}

void check_resize(XEvent *e)
{
	// ConfigureNotify is sent when the window is resized.
	if (e->type != ConfigureNotify)
		return;
	XConfigureEvent xce = e->xconfigure;
	g.xres = xce.width;
	g.yres = xce.height;
	init();
	set_window_title();
}

void clear_screen(void)
{
	// XClearWindow(dpy, win);
	XSetForeground(g.dpy, g.gc, 0x00050505);
	XFillRectangle(g.dpy, g.backBuffer, g.gc, 0, 0, g.xres, g.yres);
}

void check_mouse(XEvent *e)
{
	static int savex = 0;
	static int savey = 0;

	if (e->type != ButtonPress && e->type != ButtonRelease &&
		e->type != MotionNotify)
	{
		return;
	}
	if (e->type == ButtonRelease)
		return;
	if (e->type == ButtonPress)
	{
		// Log("ButtonPress %i %i\n", e->xbutton.x, e->xbutton.y);
		if (e->xbutton.button == 1)
		{
		}
		if (e->xbutton.button == 3)
		{
		}
	}
	if (e->type == MotionNotify)
	{
		if (savex != e->xbutton.x || savey != e->xbutton.y)
		{
			// mouse moved
			savex = e->xbutton.x;
			savey = e->xbutton.y;
		}
	}
}

int check_keys(XEvent *e)
{
	if (e->type != KeyPress && e->type != KeyRelease)
		return 0;
	int key = XLookupKeysym(&e->xkey, 0);
	if (e->type == KeyPress)
	{
		switch (key)
		{
		case XK_c:
			g.show_collisions ^= 1;
			break;
		case XK_s:
			g.slow_mode ^= 1;
			break;
		case XK_Escape:
			return 1;
		}
	}
	return 0;
}

void physics()
{
	// check for car collisions...
	g.collision_flag = 0;
	int i, j;
	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 4; j++)
		{
			if (i == j)
				continue;
			if (overlap(&cars[i], &cars[j]))
			{
				g.collision_flag = 1;
				g.collision[0] = cars[i].pos[0];
				g.collision[1] = cars[i].pos[1];
				g.collision[2] = cars[j].pos[0];
				g.collision[3] = cars[j].pos[1];
				g.crash[0] = i;
				g.crash[1] = j;
				++g.ncollisions;
			}
		}
	}
}

void render(void)
{
	clear_screen();
	XSetForeground(g.dpy, g.gc, 0x00ff0000);
	// draw intersection
	XSetForeground(g.dpy, g.gc, 0x00ffff55);
	drawRectangle(intersection.pos[0] - (intersection.w >> 1),
				  intersection.pos[1] - (intersection.h >> 1),
				  intersection.w, intersection.h);
	// draw cars
	unsigned int col[4] = {0x00ff0000, 0x0000ff00, 0x004444ff, 0x00ff00ff};
	int i;
	for (i = 0; i < 4; i++)
	{
		XSetForeground(g.dpy, g.gc, col[i]);
		fillRectangle(cars[i].pos[0] - (cars[i].w >> 1),
					  cars[i].pos[1] - (cars[i].h >> 1),
					  cars[i].w, cars[i].h);
	}
	// Key options...
	int y = 20;
	char str[100];
	sprintf(str, "'C' = see collisions");
	XSetForeground(g.dpy, g.gc, 0x0000ff00);
	drawString(20, y, str);
	y += 16;
	sprintf(str, "'S' = slow mode");
	XSetForeground(g.dpy, g.gc, 0x0000ff00);
	drawString(20, y, str);
	y += 16;
	sprintf(str, " n collisions: %i", g.ncollisions);
	XSetForeground(g.dpy, g.gc, 0x00ffff00);
	drawString(20, y, str);
	if (g.show_collisions)
	{
		if (g.collision_flag)
		{
			// show collision with lines drawn fron corner.
			XSetForeground(g.dpy, g.gc, col[g.crash[0]]);
			drawLine(g.xres - 1, 0, g.collision[0], g.collision[1]);
			XSetForeground(g.dpy, g.gc, col[g.crash[1]]);
			drawLine(g.xres - 1, 0, g.collision[2], g.collision[3]);
		}
	}
}
