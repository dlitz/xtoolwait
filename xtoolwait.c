/*
 *  Xtoolwait - wait for X client to map a window
 *  Copyright (C) 1995-1999  Richard Huveneers <richard@hekkihek.hacom.nl>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>    /* for XA_WM_STATE */
#include <X11/Xutil.h>    /* for definitions for initial window state */
#include <X11/Xmd.h>      /* for CARD32 */

#ifdef DEBUG
#   define DPRINTF(args) ((void) printf args)
#else
#   define DPRINTF(args) ((void) 0)
#endif

#define DEFAULT_TIMEOUT		15L	/* default timeout for command startup */
#define DEFAULT_MAPPINGS	1L	/* default number of mappings to wait for */
#define USAGE			"\
Usage: %s [options] command\n\
options:\n\
   -display display-name\n\
   -timeout nseconds\n\
   -mappings nwindows\n\
   -withdrawn\n\
   -pid\n\
   -help\n\
   -version\n"
#define MAINTAINER		"richard@hekkihek.hacom.nl"
#define VERSION_MAJOR		1
#define VERSION_MINOR		3

Display *dpy;
char *programname, *childname;
Atom xa_wm_state;
int (*prevxerrhandler)(Display *, XErrorEvent *);
int withdrawnok = 0;

void
timeout(signo)
    int signo;
{
    (void) fprintf(stderr, "%s: warning: timeout launching %s\n", programname, childname);
    XCloseDisplay(dpy);
    exit(1);
}

void
child_terminated(signo)
    int signo;
{
    int status;

    (void) fprintf(stderr, "%s: warning: child (%s) died prematurely\n", programname, childname);
    XCloseDisplay(dpy);
    (void) wait(&status);
    exit(WIFEXITED(status) ? WEXITSTATUS(status) : 1);
}

int
is_mapped(window)
    Window window;
{
    Atom actual_type;
    int actual_format;
    unsigned long nitems;
    unsigned long bytes_after;
    unsigned char *prop;
    int retv;
    CARD32 state;

    retv = XGetWindowProperty(dpy, window, xa_wm_state, 0L, 1L, False, xa_wm_state,
                              &actual_type, &actual_format, &nitems, &bytes_after, &prop);

    if (retv != Success)            { DPRINTF(("XGetWindowProperty failed")); return 0; }
    if (actual_type == None)        { DPRINTF(("WM_STATE undefined"));        return 0; }
    if (actual_type != xa_wm_state) { DPRINTF(("invalid WM_STATE type"));     return 0; }
    if (nitems != 1)                { DPRINTF(("invalid WM_STATE length"));   return 0; }
    if (actual_format != 32)        { DPRINTF(("invalid WM_STATE format"));   return 0; }

    state = *((CARD32 *) prop);

    switch ((int) state) {
        case WithdrawnState: DPRINTF(("WithdrawnState")); return withdrawnok;
        case NormalState:    DPRINTF(("NormalState"));    return 1;
        case IconicState:    DPRINTF(("IconicState"));    return 1;
    }

    /*
    ** We assume that the window manager knows what it is doing...
    */

    DPRINTF(("unknown WM_STATE value (%d)", (int) state));
    return 1;
}

int
xerrhandler(d, e)
    Display *d;
    XErrorEvent *e;
{
    DPRINTF(("received X error (error code %d, major opcode %d)\n", (int) e->error_code, (int) e->request_code));

    if ((int) e->error_code == BadWindow)
    {
        /*
        ** This error occurs when a window is destroyed before we got a
        ** chance to examine it.
        */
        DPRINTF(("  BadWindow error: ignored\n"));
        return 0;
    }

    return prevxerrhandler(d, e);
}

int
main(argc, argv)
    int argc;
    char **argv;
{
    int pid;
    int arg = 1;
    int printpid = 0;
    char *endptr;
    char *displayname = NULL;
    unsigned long timeouttime = DEFAULT_TIMEOUT;
    unsigned long nummappings = 1;
    XEvent event;

    programname = strrchr(argv[0], '/');
    if (programname) programname++; else programname = argv[0];

    while (1) {

        if (arg >= argc) {
            (void) fprintf(stderr, "%s: too few arguments\n", programname);
            (void) fprintf(stderr, USAGE, programname);
            return 1;
        }

        if (!strcmp(argv[arg], "-help")) {
            (void) fprintf(stdout, USAGE, programname);
            return 0;
        }

        if (!strcmp(argv[arg], "-version")) {
            (void) fprintf(stdout, "%s version %d.%d\n", programname, VERSION_MAJOR, VERSION_MINOR);
            return 0;
        }

	if (!strcmp(argv[arg], "-pid")) {
	    printpid = 1;
	    arg += 1;
	    continue;
	}

        /* the remaining options need at least one argument */
        if (arg+1 == argc) break;

        if (!strcmp(argv[arg], "-display")) {
            displayname = argv[arg+1];
            arg += 2;
            continue;
        }

        if (!strcmp(argv[arg], "-timeout")) {
            timeouttime = (unsigned long) strtol(argv[arg+1], &endptr, 0);
            if (*endptr || (endptr == argv[arg+1])) {
                (void) fprintf(stderr, "%s: invalid timeout, using default of %ld\n",
                    programname, DEFAULT_TIMEOUT);
                timeouttime = DEFAULT_TIMEOUT;
            }
            arg += 2;
            continue;
        }

        if (!strcmp(argv[arg], "-mappings")) {
            nummappings = (unsigned long) strtol(argv[arg+1], &endptr, 0);
            if (*endptr || (endptr == argv[arg+1])) {
                (void) fprintf(stderr, "%s: invalid number of mappings, using default of %ld\n",
                    programname, DEFAULT_MAPPINGS);
                nummappings = DEFAULT_MAPPINGS;
            }
            arg += 2;
            continue;
        }

	if (!strcmp(argv[arg], "-withdrawn")) {
	    withdrawnok = 1;
	    arg += 1;
	    continue;
	}
        break;
    }

    childname = argv[arg];

    prevxerrhandler = XSetErrorHandler(xerrhandler);

    dpy = XOpenDisplay(displayname);
    if (!dpy) {
        (void) fprintf(stderr, "%s: unable to open display (%s), NOT executing %s\n",
        	programname, XDisplayName(displayname), childname);
        return 1;
    }

    /*
    ** Make sure that the file descriptor is not passed to the client.
    */

    if (fcntl(ConnectionNumber(dpy), F_SETFD, 1L) == -1) {
        (void) fprintf(stderr, "%s: warning: one file descriptor unusable for ", programname);
        perror(childname);
    }
    DPRINTF(("Close on exec flag: %d\n", fcntl(ConnectionNumber(dpy), F_GETFD)));

    /*
    ** We assume that the client is not smart enough to locate the virtual root window,
    ** if there is one, so it will create its main window on the default root window.
    */

    XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureNotifyMask);

    /*
    ** We assume that the window manager provides the WM_STATE property on top-level
    ** windows, as required by ICCCM 2.0.
    ** If the window manager has not yet completed its initialisation, the WM_STATE atom
    ** might not exist, in which case we create it.
    */

#ifdef XA_WM_STATE    /* probably in X11R7 :-) */
    xa_wm_state = XA_WM_STATE;
#else
    xa_wm_state = XInternAtom(dpy, "WM_STATE", False);
#endif
#if 0
    if (xa_wm_state == None) {
        (void) fprintf(stderr, "%s: your window manager does not provide WM_STATE information.\n", programname);
        (void) fprintf(stderr, "Please report the name and version number of your window manager to %s\n", MAINTAINER);
        XCloseDisplay(dpy);
        return 1;
    }
#endif

    (void) signal(SIGALRM, timeout);
    (void) signal(SIGCHLD, child_terminated);
    (void) alarm(timeouttime);

    switch (pid = fork())
    {
    case -1:
        (void) fprintf(stderr, "%s: error forking ", programname);
        perror(childname);
	XCloseDisplay(dpy);
	return 1;
    case 0:
	if (printpid) {
	    (void) fprintf(stdout, "%u\n", getpid());
	    (void) fflush(stdout);
	    /* redirect stdout of child to stderr */
	    if (dup2(2, 1) == -1) {
		(void) fprintf(stderr, "%s: error ", programname);
		perror("redirecting stdout of child to stderr");
		return 1;
	    }
	}
	(void) execvp(argv[arg], argv + arg);
	(void) fprintf(stderr, "%s: error executing ", programname);
        perror(childname);
	return 1;
    }

    (void) signal(SIGINT, SIG_IGN);

    while (nummappings) {
        XNextEvent(dpy, &event);
        switch (event.type) {

            case CreateNotify:
                DPRINTF(("CreateNotify: %ld %ld: ", event.xcreatewindow.window, event.xcreatewindow.parent));
                if (event.xcreatewindow.send_event) {
                    DPRINTF(("send event: ignored\n"));
                    break;
                }
                if (event.xcreatewindow.override_redirect) {
                    DPRINTF(("override redirect: ignored\n"));
                    break;
                }
                XSelectInput(dpy, event.xcreatewindow.window, PropertyChangeMask);
                DPRINTF(("requested property change events\n"));

                /*
                ** BUG: PropertyNotify events generated between the CreateNotify event
                **      and the XSelectInput call are lost. It is very unlikely
                **      that the WM_STATE PropertyNotify event is generated so fast.
                **      Calling is_mapped at this point would solve this, but is
                **      not a good idea, since it delays the client startup significantly.
                */

                break;

            case PropertyNotify:
                DPRINTF(("PropertyNotify: %ld %s: ", event.xproperty.window, XGetAtomName(dpy, event.xproperty.atom)));
                if (event.xproperty.send_event) {
                    DPRINTF(("send event: ignored\n"));
                    break;
                }
                if (event.xproperty.atom != xa_wm_state) {
                    DPRINTF(("uninteresting property: ignored\n"));
                    break;
                }
                if (!is_mapped(event.xproperty.window)) {
                    DPRINTF((": ignored\n"));
                    break;
                }
                nummappings--;
                DPRINTF((": accepted\n"));
                break;

            default:
                DPRINTF(("Received uninteresting event: %d %ld: ignored\n", event.type, event.xany.window));
                break;
        }
    }

    XCloseDisplay(dpy);
    return 0;
}
