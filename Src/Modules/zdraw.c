/*
 * zdraw.c - terminal drawing and interaction module derived from zsh/curses
 *
 * SPDX-License-Identifier: LicenseRef-Zsh
 * Original upstream copyright and licence terms follow.
 *
 * Copyright (c) 2007  Clint Adams
 * All rights reserved.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and to distribute modified versions of this software for any
 * purpose, provided that the above copyright notice and the following
 * two paragraphs appear in all copies of this software.
 *
 * In no event shall Clint Adams or the Zsh Development Group be liable
 * to any party for direct, indirect, special, incidental, or consequential
 * damages arising out of the use of this software and its documentation,
 * even if Clint Adams and the Zsh Development Group have been advised of
 * the possibility of such damage.
 *
 * Clint Adams and the Zsh Development Group specifically disclaim any
 * warranties, including, but not limited to, the implied warranties of
 * merchantability and fitness for a particular purpose.  The software
 * provided hereunder is on an "as is" basis, and Clint Adams and the
 * Zsh Development Group have no obligation to provide maintenance,
 * support, updates, enhancements, or modifications.
 *
 */

/* zdraw: independent terminal drawing and interaction module, derived from zsh/curses. */

#define ZSH_CURSES_SOURCE 1

#include "zdraw.mdh"
#include "zdraw.pro"

#ifndef MULTIBYTE_SUPPORT
# undef HAVE_GETCCHAR
# undef HAVE_SETCCHAR
# undef HAVE_WADDWSTR
# undef HAVE_WGET_WCH
# undef HAVE_WIN_WCH
# undef HAVE_WADD_WCHNSTR
# undef HAVE_WBORDER_SET
# undef HAVE_NCURSESW_NCURSES_H
#endif

#ifdef ZSH_HAVE_CURSES_H
# include "../zshcurses.h"
#endif

#ifdef HAVE_SETCCHAR
# include <wchar.h>
#endif

#include <stdio.h>
#include <limits.h>
#include <time.h>
#include <locale.h>
#ifdef HAVE_LANGINFO_H
# include <langinfo.h>
#endif
#if defined(MULTIBYTE_SUPPORT) && defined(HAVE_NL_LANGINFO) && defined(CODESET)
# define ZDRAW_GRAPHEME 1
# include "zdraw_grapheme.h"
#endif

#if defined(HAVE_SETCCHAR) && defined(HAVE_GETCCHAR) && defined(HAVE_WADD_WCHNSTR) && \
    defined(HAVE_WGETBKGRND) && defined(HAVE_WBKGRNDSET) && \
    defined(HAVE_WATTR_GET) && defined(HAVE_WATTR_SET)
# define ZDRAW_WIDE_SPANS 1
#endif

/* The safe policy promises tested native storage as well as query boundaries. */
#if defined(ZDRAW_GRAPHEME) && defined(ZDRAW_WIDE_SPANS) && \
    defined(NCURSES_VERSION) && defined(HAVE_WIN_WCH)
# define ZDRAW_SAFE_GRAPHEME 1
#endif
#define ZDRAW_CELL_POLICY "libc-wcwidth"
#define ZDRAW_SAFE_POLICY "unicode-17.0.0-egc-wcwidth-sum-attach-zero"
#define ZDRAW_SAFE_BYTES 1048576
#define ZDRAW_SAFE_SPANS 4096
static int zdraw_policy_string(const char *, char **);
static int zdraw_text_policy(const char *, const char *, char *, int *);

/* Parameter setters take ownership of a permanent, deep-copied array. The
 * exported zlinklist2array gained an explicit ownership argument after
 * Zsh 5.8. Keep this small conversion independent of that internal API change. */
static char **
zdraw_list_array(LinkList list)
{
    char **result = (char **)zalloc((countlinknodes(list) + 1) * sizeof(char *));
    char **next = result;
    LinkNode node;

    for (node = firstnode(list); node; incnode(node))
        *next++ = ztrdup((char *)getdata(node));
    *next = NULL;
    return result;
}

#if defined(HAVE_NEWPAD) && defined(HAVE_PNOUTREFRESH)
# define ZDRAW_PADS 1
#endif

#if defined(HAVE_MVWIN) && defined(HAVE_WRESIZE) && \
    defined(HAVE_WATTR_GET) && defined(HAVE_WATTR_SET)
# define ZDRAW_WINDOW_RESIZE 1
#endif
#if defined(ZDRAW_PADS) && defined(HAVE_COPYWIN) && defined(HAVE_WRESIZE) && \
    defined(HAVE_WGETBKGRND) && defined(HAVE_WBKGRNDSET) && \
    defined(HAVE_WATTR_GET) && defined(HAVE_WATTR_SET)
# define ZDRAW_PAD_RESIZE 1
#endif

#define ZDRAW_RESIZE_CELLS 262144
#define ZDRAW_RESIZE_DIMENSION 32767

#if defined(ZDRAW_WINDOW_RESIZE) && defined(HAVE_WGETBKGRND) && defined(HAVE_WBKGRNDSET)
# define ZDRAW_WINDOW_TREE 1
#endif
#define ZDRAW_TREE_WINDOWS 64

#define ZDRAW_PAD_CELLS 262144
#define ZDRAW_PAD_TOTAL_CELLS 1048576
#define ZDRAW_PAD_DIMENSION 32767
static size_t zdraw_pad_cells;

/* RGB values need int, but pair IDs remain within the existing short limits. */
#if defined(NCURSES_VERSION) && defined(NCURSES_EXT_COLORS) && \
    defined(HAVE_INIT_EXTENDED_PAIR) && defined(HAVE_EXTENDED_COLOR_CONTENT) && \
    defined(HAVE_TIGETFLAG) && defined(HAVE_TIGETNUM) && defined(HAVE_TIGETSTR) && \
    INT_MAX >= 0xffffff
# define ZDRAW_TRUECOLOR 1
#endif

enum zc_win_flags {
    /* Window is permanent (probably "stdscr") */
    ZCWF_PERMANENT = 0x0001,
    /* Scrolling enabled */
    ZCWF_SCROLL = 0x0002,
    /* Offscreen surface with explicit viewport presentation. */
    ZCWF_PAD = 0x0004
};

typedef struct zc_win *ZCWin;

struct zc_win {
    WINDOW *win;
    char *name;
    int flags;
    int timeout;
    size_t pad_cells;
    LinkList children;
    ZCWin parent;
};

#ifdef ZDRAW_WINDOW_TREE
/* One bounded retired tree at most. Failed deletion is retried before another
 * rebuild, and children always outlive their backing parent's allocation. */
static WINDOW *zdraw_tree_retired[ZDRAW_TREE_WINDOWS];
static int zdraw_tree_parents[ZDRAW_TREE_WINDOWS], zdraw_tree_retired_count;
#endif
static int zdraw_tree_collect_retired(void);
static WINDOW *zdraw_screen_init(void);
static void zdraw_screen_end(void);

struct zdraw_namenumberpair {
    char *name;
    int number;
};

struct colorpairnode {
    struct hashnode node;
    short colorpair;
};
typedef struct colorpairnode *Colorpairnode;

typedef int (*zccmd_t)(const char *nam, char **args);
struct zdraw_subcommand {
    const char *name;
    zccmd_t cmd;
    int minargs;
    int maxargs;
};

static struct ttyinfo saved_tty_state;
static struct ttyinfo curses_tty_state;
static LinkList zdraw_windows;
#ifdef NCURSES_VERSION
/* A pad never participates in automatic input refresh in ncurses. Keep it
 * private: it shares the screen's input queue, not its drawing surfaces. */
static WINDOW *zdraw_input_pad;
#endif
static HashTable zdraw_colorpairs = NULL;
#ifdef NCURSES_MOUSE_VERSION
/*
 * The following is in principle a general set of flags, but
 * is currently only needed for mouse status.
 */
static int zdraw_flags;
#endif

#define ZDRAW_EINVALID 1
#define ZDRAW_EDEFINED 2
#define ZDRAW_EUNDEFINED 3

#define ZDRAW_UNUSED 1
#define ZDRAW_USED 2

#define ZDRAW_ATTRON 1
#define ZDRAW_ATTROFF 2

static int zc_errno, zc_color_phase=0;
static short next_cp=0;
/* Results of the current session's initialization, not compiled features. */
static int zc_has_colors, zc_color_started, zc_default_colors;
static int zc_can_change_color;
static int zc_truecolor, zc_truecolor_supported;
static int zc_rgb_min = -1;
static int zdraw_event_rows, zdraw_event_cols;
static int zdraw_suspended;
static int zdraw_terminal_size(int *rows, int *cols);
static void zdraw_raster_cleanup(void);
#if defined(NCURSES_VERSION) && defined(HAVE_DEFINE_KEY) && \
    defined(HAVE_KEY_DEFINED) && defined(HAVE_KEYBOUND)
# define ZDRAW_PASTE 1
static int zdraw_paste_key, zdraw_paste_active, zdraw_paste_match;
static struct ttyinfo zdraw_paste_tty_state;
#endif
#if defined(HAVE_DEF_PROG_MODE) && defined(HAVE_RESET_PROG_MODE)
# define ZDRAW_SUSPEND 1
static int zdraw_saved_mouse;
#endif
#if defined(NCURSES_VERSION) && defined(HAVE_GET_ESCDELAY) && defined(HAVE_SET_ESCDELAY)
# define ZDRAW_INPUT_DELAY 1
static int zdraw_saved_escape_delay = -1;
#endif

/* Exact capability replies reuse curses' decoder; no second input reader.
 * A mode may be queried once per session: replies carry no request identifier. */
#if defined(ZDRAW_PASTE) && defined(HAVE_CLOCK_GETTIME) && defined(CLOCK_MONOTONIC)
# define ZDRAW_QUERIES 1
# define ZDRAW_QUERY_KEYS 47
static int zdraw_query_keys[ZDRAW_QUERY_KEYS], zdraw_query_owner;
static int zdraw_query_pending = -1;
static double zdraw_query_deadline;
#endif
static const char *zdraw_query_names[] = {
    "streaming_paste", "focus_events", "synchronized_output", "keyboard_events"
};
static const int zdraw_query_modes[] = {2004, 1004, 2026, 0};
static int zdraw_query_reports[4] = {-1, -1, -1, -1};
static const char *zdraw_query_states[4] = {"never", "never", "never", "never"};
static void zdraw_query_cleanup(void);
static void zdraw_query_cancel(void);
#ifdef ZDRAW_QUERIES
static int zdraw_sync_on, zdraw_sync_applied, zdraw_presenting;
#endif
static int zdraw_sync_reset(void);
#ifdef ZDRAW_QUERIES
# define ZDRAW_ENHANCED 1
static int zdraw_focus_owned[2];
static int zdraw_focus_keys[2], zdraw_focus_on, zdraw_focus_applied;
static int zdraw_keyboard_on, zdraw_keyboard_applied;
#define ZDRAW_KEY_BYTES 256
static char zdraw_key_buffer[ZDRAW_KEY_BYTES];
static int zdraw_key_used, zdraw_key_discard;
static double zdraw_key_deadline;
#endif
static int zdraw_enhanced_pause(void);
static int zdraw_enhanced_resume(void);
static void zdraw_enhanced_cleanup(void);

enum {
    ZCF_MOUSE_ACTIVE = 1 << 0,
    ZCF_MOUSE_MASK_CHANGED = 1 << 1
};

static const struct zdraw_namenumberpair zdraw_attributes[] = {
    {"blink", A_BLINK},
    {"bold", A_BOLD},
    {"dim", A_DIM},
    {"reverse", A_REVERSE},
    {"standout", A_STANDOUT},
    {"underline", A_UNDERLINE},
    {NULL, 0}
};

static const struct zdraw_namenumberpair zdraw_colors[] = {
    {"black", COLOR_BLACK},
    {"red", COLOR_RED},
    {"green", COLOR_GREEN},
    {"yellow", COLOR_YELLOW},
    {"blue", COLOR_BLUE},
    {"magenta", COLOR_MAGENTA},
    {"cyan", COLOR_CYAN},
    {"white", COLOR_WHITE},
#ifdef HAVE_USE_DEFAULT_COLORS
    {"default", -1},
#endif
    {NULL, 0}
};

#ifdef NCURSES_MOUSE_VERSION
enum zdraw_mouse_event_types {
    ZCME_PRESSED,
    ZCME_RELEASED,
    ZCME_CLICKED,
    ZCME_DOUBLE_CLICKED,
    ZCME_TRIPLE_CLICKED
};

static const struct zdraw_namenumberpair zdraw_mouse_event_list[] = {
    {"PRESSED", ZCME_PRESSED},
    {"RELEASED", ZCME_RELEASED},
    {"CLICKED", ZCME_CLICKED},
    {"DOUBLE_CLICKED", ZCME_DOUBLE_CLICKED},
    {"TRIPLE_CLICKED", ZCME_TRIPLE_CLICKED},
    {NULL, 0}
};

struct zdraw_mouse_event {
    int button;
    int what;
    mmask_t event;
};

static const struct zdraw_mouse_event zdraw_mouse_map[] = {
    { 1, ZCME_PRESSED, BUTTON1_PRESSED },
    { 1, ZCME_RELEASED, BUTTON1_RELEASED },
    { 1, ZCME_CLICKED, BUTTON1_CLICKED },
    { 1, ZCME_DOUBLE_CLICKED, BUTTON1_DOUBLE_CLICKED },
    { 1, ZCME_TRIPLE_CLICKED, BUTTON1_TRIPLE_CLICKED },

    { 2, ZCME_PRESSED, BUTTON2_PRESSED },
    { 2, ZCME_RELEASED, BUTTON2_RELEASED },
    { 2, ZCME_CLICKED, BUTTON2_CLICKED },
    { 2, ZCME_DOUBLE_CLICKED, BUTTON2_DOUBLE_CLICKED },
    { 2, ZCME_TRIPLE_CLICKED, BUTTON2_TRIPLE_CLICKED },

    { 3, ZCME_PRESSED, BUTTON3_PRESSED },
    { 3, ZCME_RELEASED, BUTTON3_RELEASED },
    { 3, ZCME_CLICKED, BUTTON3_CLICKED },
    { 3, ZCME_DOUBLE_CLICKED, BUTTON3_DOUBLE_CLICKED },
    { 3, ZCME_TRIPLE_CLICKED, BUTTON3_TRIPLE_CLICKED },

    { 4, ZCME_PRESSED, BUTTON4_PRESSED },
    { 4, ZCME_RELEASED, BUTTON4_RELEASED },
    { 4, ZCME_CLICKED, BUTTON4_CLICKED },
    { 4, ZCME_DOUBLE_CLICKED, BUTTON4_DOUBLE_CLICKED },
    { 4, ZCME_TRIPLE_CLICKED, BUTTON4_TRIPLE_CLICKED },

#ifdef BUTTON5_PRESSED
    /* Not defined if only 32 bits available */
    { 5, ZCME_PRESSED, BUTTON5_PRESSED },
    { 5, ZCME_RELEASED, BUTTON5_RELEASED },
    { 5, ZCME_CLICKED, BUTTON5_CLICKED },
    { 5, ZCME_DOUBLE_CLICKED, BUTTON5_DOUBLE_CLICKED },
    { 5, ZCME_TRIPLE_CLICKED, BUTTON5_TRIPLE_CLICKED },
#endif
    { 0, 0, 0 }
};

static mmask_t zdraw_mouse_mask = ALL_MOUSE_EVENTS;

#endif

/* Autogenerated keypad string/number mapping*/
#include "zdraw_keys.h"

static char **
zdraw_pairs_to_array(const struct zdraw_namenumberpair *nnps)
{
    char **arr, **arrptr;
    int count;
    const struct zdraw_namenumberpair *nnptr;

    for (nnptr = nnps; nnptr->name; nnptr++)
	;
    count = nnptr - nnps;

    arrptr = arr = (char **)zhalloc((count+1) * sizeof(char *));

    for (nnptr = nnps; nnptr->name; nnptr++)
	*arrptr++ = dupstring(nnptr->name);
    *arrptr = NULL;

    return arr;
}

static const char *
zdraw_strerror(int err)
{
    static const char *errs[] = {
	"unknown error",
	"window name invalid",
	"window already defined",
	"window undefined",
	NULL };

    return errs[(err < 1 || err > 3) ? 0 : err];
}

/* Dispatch checks stdscr before looking up the drawing target. Retain both
 * recent lookups so that check cannot evict a repeatedly used target. The
 * list remains authoritative, including its public order. */
static LinkNode zdraw_last_window, zdraw_previous_window;

static LinkNode
zdraw_getwindowbyname(const char *name)
{
    LinkNode node;
    ZCWin w;

    if (zdraw_last_window &&
        !strcmp(((ZCWin)getdata(zdraw_last_window))->name, name))
        return zdraw_last_window;
    if (zdraw_previous_window &&
        !strcmp(((ZCWin)getdata(zdraw_previous_window))->name, name)) {
        node = zdraw_previous_window;
        zdraw_previous_window = zdraw_last_window;
        return zdraw_last_window = node;
    }
    for (node = firstnode(zdraw_windows); node; incnode(node))
	if (w = (ZCWin)getdata(node), !strcmp(w->name, name)) {
            zdraw_previous_window = zdraw_last_window;
	    return zdraw_last_window = node;
        }

    return NULL;
}

static LinkNode
zdraw_validate_window(char *win, int criteria)
{
    LinkNode target;

    if (win==NULL || !*win) {
	zc_errno = ZDRAW_EINVALID;
	return NULL;
    }

    target = zdraw_getwindowbyname(win);

    if (target && (criteria & ZDRAW_UNUSED)) {
	zc_errno = ZDRAW_EDEFINED;
	return NULL;
    }

    if (!target && (criteria & ZDRAW_USED)) {
	zc_errno = ZDRAW_EUNDEFINED;
	return NULL;
    }

    zc_errno = 0;
    return target;
}

static int
zdraw_free_window(ZCWin w)
{
    int ret = 0;

    /* Callers may already have unlinked/freed the list node. */
    zdraw_last_window = zdraw_previous_window = NULL;

    if (!(w->flags & ZCWF_PERMANENT) && delwin(w->win)!=OK) {
	DPUTS2(1, "BUG: Failed to delete ncurses window %s with %d children",
		w->name ? w->name : "(null)",
		w->children ? countlinknodes(w->children) : 0);
	ret = 1;
    }

    zdraw_pad_cells -= w->pad_cells;
    if (w->name)
	zsfree(w->name);

    if (w->children)
	freelinklist(w->children, (FreeFunc)NULL);

    zfree(w, sizeof(struct zc_win));

    return ret;
}

static struct zdraw_namenumberpair *
zdraw_attrget(UNUSED(WINDOW *w), char *attr)
{
    struct zdraw_namenumberpair *zca;

    if (!attr)
	return NULL;

    for(zca=(struct zdraw_namenumberpair *)zdraw_attributes;zca->name;zca++)
	if (!strcmp(attr, zca->name)) {
	    return zca;
	}

    return NULL;
}

static int
zdraw_color(const char *color)
{
    struct zdraw_namenumberpair *zc;
    const char *p;
    int value = 0;

    if (*color == '#') {
	if (!zc_truecolor || strlen(color) != 7)
	    return -2;
	for (p = color + 1; *p; p++) {
	    int digit;
	    if (*p >= '0' && *p <= '9')
		digit = *p - '0';
	    else if (*p >= 'a' && *p <= 'f')
		digit = *p - 'a' + 10;
	    else if (*p >= 'A' && *p <= 'F')
		digit = *p - 'A' + 10;
	    else
		return -2;
	    value = value * 16 + digit;
	}
	return value >= zc_rgb_min ? value : -2;
    }

    /* Validate the entire decimal value before converting to curses' short.
     * In particular, atoi() would accept suffixes and could overflow. */
    if (*color >= '0' && *color <= '9') {
	for (p = color; *p; p++) {
	    if (*p < '0' || *p > '9' ||
		value > (SHRT_MAX - (*p - '0')) / 10)
		return (short)-2;
	    value = value * 10 + (*p - '0');
	}
	return value < COLORS ? (short)value : (short)-2;
    }

    for(zc=(struct zdraw_namenumberpair *)zdraw_colors;zc->name;zc++)
	if (!strcmp(color, zc->name)) {
	    return (short)zc->number;
	}

    return (short)-2;
}

/* Number of nonzero pair IDs representable by both curses and this module.
 * Keep allocation and runtime reporting on the same boundary. */
static int
zdraw_pair_limit(void)
{
    if (!zc_color_started || COLOR_PAIRS <= 1)
	return 0;
    return COLOR_PAIRS - 1 < SHRT_MAX ? COLOR_PAIRS - 1 : SHRT_MAX;
}

static Colorpairnode
zdraw_colorget(const char *nam, char *colorpair)
{
    char *bg, *cp;
    int f, b, result;
    Colorpairnode cpn;

    /* zdraw_colorpairs is only initialised if color is supported */
    if (!zdraw_colorpairs || (!zc_truecolor && strchr(colorpair, '#')))
	return NULL;

    if (zc_color_phase==1 ||
	!(cpn = (Colorpairnode) gethashnode2(zdraw_colorpairs, colorpair))) {
	zc_color_phase = 2;
	cp = ztrdup(colorpair);

	bg = strchr(cp, '/');
	if (bg==NULL) {
	    zsfree(cp);
	    return NULL;
	}

	*bg = '\0';        

	f = zdraw_color(cp);
	b = zdraw_color(bg+1);

	if (f==-2 || b==-2) {
	    if (f == -2)
		zwarnnam(nam, "foreground color `%s' not known", cp);
	    if (b == -2)
		zwarnnam(nam, "background color `%s' not known", bg+1);
	    *bg = '/';
	    zsfree(cp);
	    return NULL;
	}
	*bg = '/';

	/* The library may advertise more pairs than this interface can hold.
	 * Check before incrementing, and never recycle a pair used by cells. */
	if (next_cp >= zdraw_pair_limit()) {
	    zsfree(cp);
	    return NULL;
	}

	cpn = (Colorpairnode)zshcalloc(sizeof(struct colorpairnode));
	
	if (!cpn) {
	    zsfree(cp);
	    return NULL;
	}
	/* Decimal indices keep their old bounds and allocation path. */
#ifdef ZDRAW_TRUECOLOR
	if (strchr(colorpair, '#'))
	    result = init_extended_pair((int)next_cp + 1, f, b);
	else
#endif
	    result = init_pair((short)(next_cp + 1), (short)f, (short)b);
	if (result == ERR) {
	    zfree(cpn, sizeof(struct colorpairnode));
	    zsfree(cp);
	    return NULL;
	}

	cpn->colorpair = ++next_cp;
	addhashnode(zdraw_colorpairs, cp, (void *)cpn);
    }

    return cpn;
}

static Colorpairnode cpn_match;

static void
zdraw_colornode(HashNode hn, int cp)
{
    Colorpairnode cpn = (Colorpairnode)hn;
    if (cpn->colorpair == (short)cp)
	cpn_match = cpn;
}

static Colorpairnode
zdraw_colorget_reverse(short cp)
{
    if (!zdraw_colorpairs)
	return NULL;

    /* Pair IDs are immutable for the session; adjacent inspected cells often
     * have the same pair. Invalidate this pointer when its node is freed. */
    if (cpn_match && cpn_match->colorpair == cp)
        return cpn_match;
    cpn_match = NULL;
    scanhashtable(zdraw_colorpairs, 0, 0, 0,
		  zdraw_colornode, cp);
    return cpn_match;
}

static void
freecolorpairnode(HashNode hn)
{
    if ((Colorpairnode)hn == cpn_match)
        cpn_match = NULL;
    zsfree(hn->nam);
    zfree(hn, sizeof(struct colorpairnode));
}


/*************
 * Subcommands
 *************/

/* Inspect the initialized library and terminal description, never the wire.
 * Accept only 8/8/8 direct encoding; COLORS alone does not establish RGB. */
static void
zdraw_truecolor_detect(void)
{
    zc_truecolor = zc_truecolor_supported = 0;
    zc_rgb_min = -1;
#ifdef ZDRAW_TRUECOLOR
    if (zc_has_colors && zc_color_started && COLORS == 0x1000000) {
	char *encoding = tigetstr("RGB");
	char *fg = tigetstr("setaf"), *bg = tigetstr("setab");
	int r, g, b, minimum = tigetnum("CO");
	if (!(tigetflag("RGB") == 1 || tigetnum("RGB") == 8 ||
	      (encoding && encoding != (char *)-1 && !strcmp(encoding, "8/8/8"))) ||
	    !fg || fg == (char *)-1 || !*fg ||
	    !bg || bg == (char *)-1 || !*bg)
	    return;
	/* Confirm that the library actually interprets direct 24-bit colors.
	 * This is a read-only query, not a palette modification or allocation. */
	if (extended_color_content(0x123456, &r, &g, &b) == ERR ||
	    r != 1000 * 0x12 / 255 || g != 1000 * 0x34 / 255 || b != 1000 * 0x56 / 255)
	    return;
	/* CO describes the low indices reserved for ANSI colors in direct
	 * entries. Without it, retain the conventional eight-index reservation. */
	if (minimum < 0)
	    minimum = 8;
	if (minimum > 0xffffff)
	    return;
	zc_rgb_min = minimum;
	zc_truecolor_supported = 1;
    }
#endif
}

static int
zccmd_truecolor(const char *nam, char **args)
{
    if (!strcmp(args[0], "off")) {
	/* Existing cells and pairs remain valid; only new RGB arguments stop
	 * being accepted, including references to already cached RGB pairs. */
	zc_truecolor = 0;
	return 0;
    }
    if (strcmp(args[0], "on")) {
	zwarnnam(nam, "truecolor expects on or off");
	return 1;
    }
    if (!zc_truecolor_supported)
	return 2;
    zc_truecolor = 1;
    return 0;
}

static int
zccmd_init(UNUSED(const char *nam), UNUSED(char **args))
{
    LinkNode stdscr_win = zdraw_getwindowbyname("stdscr");

    if (!stdscr_win) {
	ZCWin w = (ZCWin)zshcalloc(sizeof(struct zc_win));
	if (!w)
	    return 1;

	gettyinfo(&saved_tty_state);
	w->name = ztrdup("stdscr");
	w->win = zdraw_screen_init();
	if (w->win == NULL) {
	    zsfree(w->name);
	    zfree(w, sizeof(struct zc_win));
	    return 1;
	}
	getmaxyx(w->win, zdraw_event_rows, zdraw_event_cols);
	w->flags = ZCWF_PERMANENT;
        w->timeout = -1;
	zinsertlinknode(zdraw_windows, lastnode(zdraw_windows), (void *)w);
	zc_has_colors = has_colors() != 0;
	zc_can_change_color = can_change_color() != 0;
	zc_color_started = zc_default_colors = 0;
	if (start_color() != ERR) {
	    Colorpairnode cpn;
	    zc_color_started = 1;

	    if(!zc_color_phase)
		zc_color_phase = 1;
	    zdraw_colorpairs = newhashtable(8, "zc_colorpairs", NULL);

	    zdraw_colorpairs->hash        = hasher;
	    zdraw_colorpairs->emptytable  = emptyhashtable;
	    zdraw_colorpairs->filltable   = NULL;
	    zdraw_colorpairs->cmpnodes    = strcmp;
	    zdraw_colorpairs->addnode     = addhashnode;
	    zdraw_colorpairs->getnode     = gethashnode2;
	    zdraw_colorpairs->getnode2    = gethashnode2;
	    zdraw_colorpairs->removenode  = removehashnode;
	    zdraw_colorpairs->disablenode = NULL;
	    zdraw_colorpairs->enablenode  = NULL;
	    zdraw_colorpairs->freenode    = freecolorpairnode;
	    zdraw_colorpairs->printnode   = NULL;

#ifdef HAVE_USE_DEFAULT_COLORS
	    zc_default_colors = use_default_colors() != ERR;
#endif
	    /* Initialise the default color pair, always 0 */
	    cpn = (Colorpairnode)zshcalloc(sizeof(struct colorpairnode));
	    if (cpn) {
		cpn->colorpair = 0;
		addhashnode(zdraw_colorpairs,
			    ztrdup("default/default"), (void *)cpn);
	    }
	}
	zdraw_truecolor_detect();
	/*
	 * We use cbreak mode because we don't want line buffering
	 * on input since we'd just need to loop over characters.
	 * We use noecho since the manual says that's the right
	 * thing to do with cbreak.
	 *
	 * Turn these on immediately to catch typeahead.
	 */
	cbreak();
	noecho();
	gettyinfo(&curses_tty_state);
    } else {
	settyinfo(&curses_tty_state);
    }
    return 0;
}


static int
zccmd_addwin(const char *nam, char **args)
{
    int nlines, ncols, begin_y, begin_x;
    ZCWin w;

    if (zdraw_validate_window(args[0], ZDRAW_UNUSED) == NULL &&
	zc_errno) {
	zwarnnam(nam, "%s: %s", zdraw_strerror(zc_errno), args[0]);
	return 1;
    }

    nlines = atoi(args[1]);
    ncols = atoi(args[2]);
    begin_y = atoi(args[3]);
    begin_x = atoi(args[4]);

    w = (ZCWin)zshcalloc(sizeof(struct zc_win));
    if (!w)
	return 1;

    w->name = ztrdup(args[0]);
    w->timeout = -1;
    if (args[5]) {
	LinkNode node;
	ZCWin worig;

	node = zdraw_validate_window(args[5], ZDRAW_USED);
	if (node == NULL) {
	    zwarnnam(nam, "%s: %s", zdraw_strerror(zc_errno), args[5]);
	    zsfree(w->name);
	    zfree(w, sizeof(struct zc_win));
	    return 1;
	}

	worig = (ZCWin)getdata(node);
        if (worig->flags & ZCWF_PAD) {
            zwarnnam(nam, "addwin does not create subwindows of pads");
            zsfree(w->name);
            zfree(w, sizeof(struct zc_win));
            return 1;
        }

	w->win = subwin(worig->win, nlines, ncols, begin_y, begin_x);
	if (w->win) {
	    w->parent = worig;
	    if (!worig->children)
		worig->children = znewlinklist();
	    zinsertlinknode(worig->children, lastnode(worig->children),
			    (void *)w);
	}
    } else {
	w->win = newwin(nlines, ncols, begin_y, begin_x);
    }

    if (w->win == NULL) {
	zwarnnam(nam, "failed to create window `%s'", w->name);
	zsfree(w->name);
	zfree(w, sizeof(struct zc_win));
	return 1;
    }

    /* prepend window so that freelinklist will free children before parents,
     * otherwise they will be leaked */
    zinsertlinknode(zdraw_windows, (LinkNode)zdraw_windows, (void *)w);

    return 0;
}

static int
zccmd_delwin(const char *nam, char **args)
{
    LinkNode node;
    ZCWin w;
    int ret = 0;

    node = zdraw_validate_window(args[0], ZDRAW_USED);
    if (node == NULL) {
	zwarnnam(nam, "%s: %s", zdraw_strerror(zc_errno), args[0]);
	return 1;
    }

    w = (ZCWin)getdata(node);

    if (w == NULL) {
	zwarnnam(nam, "record for window `%s' is corrupt", args[0]);
	return 1;
    }
    if (w->flags & ZCWF_PERMANENT) {
	zwarnnam(nam, "window `%s' can't be deleted", args[0]);
	return 1;
    }

    if (w->children && firstnode(w->children)) {
	zwarnnam(nam, "window `%s' has subwindows, delete those first",
		 w->name);
	return 1;
    }

    if (delwin(w->win)!=OK) {
        /* New pad handles retain ownership and their budget on failure. */
        if (w->flags & ZCWF_PAD)
            return 1;
	/*
	 * Not sure what to do here, but we are probably stuffed,
	 * so delete the window locally anyway.
	 */
	ret = 1;
    }

    if (w->parent) {
	/* Remove from parent's list of children */
	LinkList wpc = w->parent->children;
	LinkNode pcnode;
	for (pcnode = firstnode(wpc); pcnode; incnode(pcnode)) {
	    ZCWin child = (ZCWin)getdata(pcnode);
	    if (child == w) {
		remnode(wpc, pcnode);
		break;
	    }
	}
	DPUTS(pcnode == NULL, "BUG: child node not found in parent's children");
	/*
	 * We need to touch the parent to get the parent to refresh
	 * properly.
	 */
	touchwin(w->parent->win);
    }
    else if (!(w->flags & ZCWF_PAD))
	touchwin(stdscr);

    zdraw_pad_cells -= w->pad_cells;
    if (w->name)
	zsfree(w->name);

    if (w->children)
	freelinklist(w->children, (FreeFunc)NULL);

    zdraw_last_window = zdraw_previous_window = NULL;
    zfree((ZCWin)remnode(zdraw_windows, node), sizeof(struct zc_win));

    return ret;
}


static int
zccmd_refresh(const char *nam, char **args)
{
    WINDOW *win;
    int ret = 0;

    if (args[0]) {
	for (; *args; args++) {
	    LinkNode node;
	    ZCWin w;

	    node = zdraw_validate_window(args[0], ZDRAW_USED);
	    if (node == NULL) {
		zwarnnam(nam, "%s: %s", zdraw_strerror(zc_errno), args[0]);
		return 1;
	    }

	    w = (ZCWin)getdata(node);

	    if (w->parent) {
		/* This is what the manual says you have to do. */
		touchwin(w->parent->win);
	    }
	    win = w->win;
	    if (wnoutrefresh(win) != OK)
		ret = 1;
	}
	return (doupdate() != OK || ret);
    }
    else
    {
	return (wrefresh(stdscr) != OK) ? 1 : 0;
    }
}


static int
zccmd_move(const char *nam,  char **args)
{
    int y, x;
    LinkNode node;
    ZCWin w;

    node = zdraw_validate_window(args[0], ZDRAW_USED);
    if (node == NULL) {
	zwarnnam(nam, "%s: %s", zdraw_strerror(zc_errno), args[0]);
	return 1;
    }

    y = atoi(args[1]);
    x = atoi(args[2]);

    w = (ZCWin)getdata(node);

    if (wmove(w->win, y, x)!=OK)
	return 1;

    return 0;
}


static int
zccmd_clear(const char *nam, char **args)
{
    LinkNode node;
    ZCWin w;

    node = zdraw_validate_window(args[0], ZDRAW_USED);
    if (node == NULL) {
	zwarnnam(nam, "%s: %s", zdraw_strerror(zc_errno), args[0]);
	return 1;
    }

    w = (ZCWin)getdata(node);

    if (!args[1]) {
	return werase(w->win) != OK;
    } else if (!strcmp(args[1], "redraw")) {
	return wclear(w->win) != OK;
    } else if (!strcmp(args[1], "eol")) {
	return wclrtoeol(w->win) != OK;
    } else if (!strcmp(args[1], "bot")) {
	return wclrtobot(w->win) != OK;
    } else {
	zwarnnam(nam, "`clear' expects `redraw', `eol' or `bot'");
	return 1;
    }
}


static int
zccmd_char(const char *nam, char **args)
{
    LinkNode node;
    ZCWin w;
#ifdef HAVE_SETCCHAR
    wchar_t c[2];
    wint_t wc;
    cchar_t cc;
#endif

    node = zdraw_validate_window(args[0], ZDRAW_USED);
    if (node == NULL) {
	zwarnnam(nam, "%s: %s", zdraw_strerror(zc_errno), args[0]);
	return 1;
    }

    w = (ZCWin)getdata(node);

#ifdef HAVE_SETCCHAR
    mb_charinit();
    if (!*args[1] || !mb_metacharlenconv(args[1], &wc) ||
	wc == WEOF || wc == 0)
	return 1;
    c[0] = wc;
    c[1] = L'\0';

    if (setcchar(&cc, c, A_NORMAL, 0, NULL)==ERR)
	return 1;

    if (wadd_wch(w->win, &cc)!=OK)
	return 1;
#else
    if (waddch(w->win, (chtype)args[1][0])!=OK)
	return 1;
#endif

    return 0;
}


static int
zccmd_string(const char *nam, char **args)
{
    LinkNode node;
    ZCWin w;

#ifdef HAVE_WADDWSTR
    int clen;
    wint_t wc;
    wchar_t *wstr, *wptr;
    char *str = args[1];
#endif

    if (args[2])
        return zdraw_policy_string(nam, args);

    node = zdraw_validate_window(args[0], ZDRAW_USED);
    if (node == NULL) {
	zwarnnam(nam, "%s: %s", zdraw_strerror(zc_errno), args[0]);
	return 1;
    }

    w = (ZCWin)getdata(node);

#ifdef HAVE_WADDWSTR
    mb_charinit();
    wptr = wstr = zhalloc((strlen(str)+1) * sizeof(wchar_t));

    while (*str && (clen = mb_metacharlenconv(str, &wc))) {
	str += clen;
	if (wc == WEOF) /* TODO: replace with space? nicen? */
	    continue;
	*wptr++ = wc;
    }
    *wptr++ = L'\0';
    if (waddwstr(w->win, wstr)!=OK) {
	return 1;
    }
#else
    if (waddstr(w->win, args[1])!=OK)
	return 1;
#endif
    return 0;
}



static int
zdraw_nonnegative(char *str, int *value)
{
    int n = 0;
    if (!*str)
	return 1;
    for (; *str; str++) {
	if (*str < '0' || *str > '9' || n > (INT_MAX - (*str - '0')) / 10)
	    return 1;
	n = n * 10 + (*str - '0');
    }
    *value = n;
    return 0;
}


#ifdef HAVE_MVWIN
static ZCWin
zdraw_independent_window(const char *nam, char *name)
{
    LinkNode node = zdraw_validate_window(name, ZDRAW_USED);
    ZCWin w;
    if (!node) {
        zwarnnam(nam, "%s: %s", zdraw_strerror(zc_errno), name);
        return NULL;
    }
    w = (ZCWin)getdata(node);
    if ((w->flags & (ZCWF_PERMANENT | ZCWF_PAD)) || w->parent ||
        (w->children && firstnode(w->children))) {
        zwarnnam(nam, "requires an independent ordinary window without children: %s", name);
        return NULL;
    }
    return w;
}

static int
zdraw_window_fits(int row, int col, int rows, int cols)
{
    int screen_rows, screen_cols;
    getmaxyx(stdscr, screen_rows, screen_cols);
    return row >= 0 && col >= 0 && rows > 0 && cols > 0 &&
        row < screen_rows && col < screen_cols &&
        rows <= screen_rows - row && cols <= screen_cols - col;
}
#endif

static int
zccmd_movewin(const char *nam, char **args)
{
#ifdef HAVE_MVWIN
    ZCWin w;
    int row, col, rows, cols;
    if (zdraw_nonnegative(args[1], &row) ||
        zdraw_nonnegative(args[2], &col)) {
        zwarnnam(nam, "movewin expects nonnegative decimal coordinates");
        return 1;
    }
    w = zdraw_independent_window(nam, args[0]);
    if (!w)
        return 1;
    getmaxyx(w->win, rows, cols);
    if (!zdraw_window_fits(row, col, rows, cols)) {
        zwarnnam(nam, "moved window does not fit inside the screen");
        return 1;
    }
    return mvwin(w->win, row, col) == ERR;
#else
    (void)nam;
    (void)args;
    return 2;
#endif
}

static int
zccmd_resizewin(const char *nam, char **args)
{
#ifdef ZDRAW_WINDOW_RESIZE
    ZCWin w;
    WINDOW *replacement;
    int rows, cols, row, col, oldrows, oldcols, y, x, nargs = arrlen(args);
    attr_t attrs;
    short pair;
    if ((nargs != 3 && nargs != 5) ||
        zdraw_nonnegative(args[1], &rows) ||
        zdraw_nonnegative(args[2], &cols) || !rows || !cols ||
        rows > ZDRAW_RESIZE_DIMENSION || cols > ZDRAW_RESIZE_DIMENSION ||
        rows > ZDRAW_RESIZE_CELLS / cols) {
        zwarnnam(nam, "resizewin expects bounded positive dimensions and optional row/column");
        return 1;
    }
    w = zdraw_independent_window(nam, args[0]);
    if (!w)
        return 1;
    getbegyx(w->win, row, col);
    if (nargs == 5 && (zdraw_nonnegative(args[3], &row) ||
                      zdraw_nonnegative(args[4], &col))) {
        zwarnnam(nam, "resizewin expects nonnegative decimal coordinates");
        return 1;
    }
    if (!zdraw_window_fits(row, col, rows, cols)) {
        zwarnnam(nam, "resized window does not fit inside the screen");
        return 1;
    }
    getmaxyx(w->win, oldrows, oldcols);
    if (oldrows <= 0 || oldcols <= 0 || oldrows > ZDRAW_RESIZE_CELLS / oldcols) {
        zwarnnam(nam, "existing window exceeds the resize copy limit");
        return 1;
    }
    getyx(w->win, y, x);
    if (y >= rows)
        y = rows - 1;
    if (x >= cols)
        x = cols - 1;
    if (wattr_get(w->win, &attrs, &pair, NULL) == ERR)
        return 1;
    replacement = dupwin(w->win);
    if (!replacement) {
        zwarnnam(nam, "failed to duplicate window for resizing");
        return 1;
    }
    /* Work on an independent copy, including for resize-and-move recovery
     * after terminal shrink. Explicitly restore the full current pair: some
     * dupwin implementations copy packed attributes but lose extended IDs. */
    if (wresize(replacement, rows, cols) == ERR ||
        mvwin(replacement, row, col) == ERR ||
        wattr_set(replacement, attrs, pair, NULL) == ERR ||
        wmove(replacement, y, x) == ERR ||
        scrollok(replacement, (w->flags & ZCWF_SCROLL) != 0) == ERR) {
        delwin(replacement);
        return 1;
    }
    wtimeout(replacement, w->timeout);
    if (delwin(w->win) == ERR) {
        delwin(replacement);
        return 1;
    }
    w->win = replacement;
    return 0;
#else
    (void)nam;
    (void)args;
    return 2;
#endif
}

static int
zdraw_tree_collect_retired(void)
{
#ifdef ZDRAW_WINDOW_TREE
    int i, j, blocked, pending = 0;
    for (i = zdraw_tree_retired_count - 1; i >= 0; i--) {
        if (!zdraw_tree_retired[i]) continue;
        blocked = 0;
        for (j = i + 1; j < zdraw_tree_retired_count; j++)
            if (zdraw_tree_retired[j] && zdraw_tree_parents[j] == i) blocked = 1;
        if (!blocked && delwin(zdraw_tree_retired[i]) != ERR)
            zdraw_tree_retired[i] = NULL;
        else pending = 1;
    }
    if (!pending) zdraw_tree_retired_count = 0;
    return pending;
#else
    return 0;
#endif
}

static int
zccmd_treewin(const char *nam, char **args)
{
#ifdef ZDRAW_WINDOW_TREE
    struct tree_entry {
        ZCWin w;
        WINDOW *replacement;
        int parent, row, col, rows, cols;
    } entries[ZDRAW_TREE_WINDOWS];
    LinkNode node, child;
    ZCWin target, root;
    int rows, cols, row, col, i, n = 1, j, py, px, cy, cx, oldrows, oldcols;
    int y, x, result = 1;
    attr_t attrs;
    short pair;
    cchar_t background;
    if (zdraw_nonnegative(args[1], &rows) || zdraw_nonnegative(args[2], &cols) ||
        zdraw_nonnegative(args[3], &row) || zdraw_nonnegative(args[4], &col) ||
        !rows || !cols || rows > ZDRAW_RESIZE_DIMENSION || cols > ZDRAW_RESIZE_DIMENSION ||
        rows > ZDRAW_RESIZE_CELLS / cols)
        return 1;
    node = zdraw_validate_window(args[0], ZDRAW_USED);
    if (!node) return 1;
    target = root = (ZCWin)getdata(node);
    for (i = 0; root->parent; i++, root = root->parent)
        if (i >= ZDRAW_TREE_WINDOWS) return 1;
    if (root->flags & (ZCWF_PERMANENT | ZCWF_PAD)) {
        zwarnnam(nam, "treewin requires a tree rooted in an ordinary owned window");
        return 1;
    }
    if (zdraw_tree_collect_retired()) {
        zwarnnam(nam, "could not release the previous retired window tree");
        return 1;
    }
    memset(entries, 0, sizeof(entries));
    entries[0].w = root;
    entries[0].parent = -1;
    /* Breadth-first collection bounds both depth and work; no C recursion. */
    for (i = 0; i < n; i++) {
        ZCWin w = entries[i].w;
        getmaxyx(w->win, entries[i].rows, entries[i].cols);
        getbegyx(w->win, cy, cx);
        getmaxyx(w->win, oldrows, oldcols);
        if (oldrows <= 0 || oldcols <= 0 || oldrows > ZDRAW_RESIZE_CELLS / oldcols)
            return 1;
        if (w == target) {
            entries[i].rows = rows; entries[i].cols = cols;
            entries[i].row = row; entries[i].col = col;
        } else if (i) {
            j = entries[i].parent;
            getbegyx(entries[j].w->win, py, px);
            /* Existing relative offsets remain fixed when an ancestor moves. */
            if (cy < py || cx < px || cy - py > INT_MAX - entries[j].row ||
                cx - px > INT_MAX - entries[j].col) return 1;
            entries[i].row = entries[j].row + cy - py;
            entries[i].col = entries[j].col + cx - px;
        } else {
            entries[i].row = cy; entries[i].col = cx;
        }
        if (!zdraw_window_fits(entries[i].row, entries[i].col, entries[i].rows, entries[i].cols))
            return 1;
        if (i) {
            j = entries[i].parent;
            py = entries[i].row - entries[j].row; px = entries[i].col - entries[j].col;
            if (py < 0 || px < 0 || py >= entries[j].rows || px >= entries[j].cols ||
                entries[i].rows > entries[j].rows - py || entries[i].cols > entries[j].cols - px)
                return 1;
        }
        if (w->children) for (child = firstnode(w->children); child; incnode(child)) {
            if (n == ZDRAW_TREE_WINDOWS) {
                zwarnnam(nam, "treewin exceeds the 64-window tree limit");
                return 1;
            }
            entries[n].w = (ZCWin)getdata(child);
            entries[n++].parent = i;
        }
    }
    /* Prepare independent backing first, then shared views. Live cells and
     * handles remain untouched until every replacement and mode is ready. */
    queue_signals();
    for (i = 0; i < n; i++) {
        ZCWin w = entries[i].w;
        WINDOW *replacement;
        getyx(w->win, y, x);
        if (y >= entries[i].rows) y = entries[i].rows - 1;
        if (x >= entries[i].cols) x = entries[i].cols - 1;
        if (wattr_get(w->win, &attrs, &pair, NULL) == ERR || wgetbkgrnd(w->win, &background) == ERR)
            goto cleanup;
        if (!i) {
            replacement = dupwin(w->win);
            entries[i].replacement = replacement;
            if (!replacement || wresize(replacement, entries[i].rows, entries[i].cols) == ERR ||
                mvwin(replacement, entries[i].row, entries[i].col) == ERR) goto cleanup;
        } else {
            replacement = subwin(entries[entries[i].parent].replacement,
                entries[i].rows, entries[i].cols, entries[i].row, entries[i].col);
            entries[i].replacement = replacement;
            if (!replacement) goto cleanup;
        }
        wbkgrndset(replacement, &background);
        if (wattr_set(replacement, attrs, pair, NULL) == ERR || wmove(replacement, y, x) == ERR ||
            scrollok(replacement, (w->flags & ZCWF_SCROLL) != 0) == ERR) goto cleanup;
        wtimeout(replacement, w->timeout);
    }
    /* Publishing cannot fail. Retirement may fail afterward: retain coherent
     * new handles and a bounded cleanup obligation instead of dangling children. */
    for (i = 0; i < n; i++) {
        zdraw_tree_retired[i] = entries[i].w->win;
        zdraw_tree_parents[i] = entries[i].parent;
        entries[i].w->win = entries[i].replacement;
    }
    zdraw_tree_retired_count = n;
    if (zdraw_tree_collect_retired()) {
        zwarnnam(nam, "treewin applied geometry but old-tree cleanup needs retry");
        unqueue_signals();
        return 1;
    }
    unqueue_signals();
    return 0;
cleanup:
    for (j = 0; j < n; j++) {
        zdraw_tree_retired[j] = entries[j].replacement;
        zdraw_tree_parents[j] = entries[j].parent;
    }
    zdraw_tree_retired_count = n;
    zdraw_tree_collect_retired();
    unqueue_signals();
    return result;
#else
    (void)nam; (void)args;
    return 2;
#endif
}

static int
zccmd_addpad(const char *nam, char **args)
{
#ifdef ZDRAW_PADS
    ZCWin w;
    WINDOW *pad;
    int rows, cols;
    size_t cells;
    if (zdraw_validate_window(args[0], ZDRAW_UNUSED) == NULL && zc_errno) {
        zwarnnam(nam, "%s: %s", zdraw_strerror(zc_errno), args[0]);
        return 1;
    }
    if (zdraw_nonnegative(args[1], &rows) ||
        zdraw_nonnegative(args[2], &cols) || !rows || !cols ||
        rows > ZDRAW_PAD_DIMENSION || cols > ZDRAW_PAD_DIMENSION ||
        rows > ZDRAW_PAD_CELLS / cols) {
        zwarnnam(nam, "addpad expects positive dimensions within the pad limits");
        return 1;
    }
    cells = (size_t)rows * cols;
    if (cells > ZDRAW_PAD_TOTAL_CELLS - zdraw_pad_cells) {
        zwarnnam(nam, "pads exceed the session cell limit");
        return 1;
    }
    pad = newpad(rows, cols);
    if (!pad) {
        zwarnnam(nam, "failed to allocate pad: %s", args[0]);
        return 1;
    }
    w = (ZCWin)zshcalloc(sizeof(struct zc_win));
    w->win = pad;
    w->name = ztrdup(args[0]);
    w->timeout = -1;
    w->flags = ZCWF_PAD;
    w->pad_cells = cells;
    zdraw_pad_cells += cells;
    zinsertlinknode(zdraw_windows, (LinkNode)zdraw_windows, (void *)w);
    return 0;
#else
    (void)nam;
    (void)args;
    return 2;
#endif
}

static int
zccmd_resizepad(const char *nam, char **args)
{
#ifdef ZDRAW_PAD_RESIZE
    LinkNode node;
    ZCWin w;
    WINDOW *replacement;
    cchar_t background;
    attr_t attrs;
    short pair;
    int rows, cols, oldrows, oldcols, y, x;
    size_t cells;
    if (zdraw_nonnegative(args[1], &rows) ||
        zdraw_nonnegative(args[2], &cols) || !rows || !cols ||
        rows > ZDRAW_PAD_DIMENSION || cols > ZDRAW_PAD_DIMENSION ||
        rows > ZDRAW_PAD_CELLS / cols) {
        zwarnnam(nam, "resizepad expects positive dimensions within the pad limits");
        return 1;
    }
    node = zdraw_validate_window(args[0], ZDRAW_USED);
    if (!node) {
        zwarnnam(nam, "%s: %s", zdraw_strerror(zc_errno), args[0]);
        return 1;
    }
    w = (ZCWin)getdata(node);
    if (!(w->flags & ZCWF_PAD)) {
        zwarnnam(nam, "resizepad requires a pad: %s", args[0]);
        return 1;
    }
    cells = (size_t)rows * cols;
    if (cells > ZDRAW_PAD_TOTAL_CELLS - (zdraw_pad_cells - w->pad_cells)) {
        zwarnnam(nam, "resized pad exceeds the session cell limit");
        return 1;
    }
    getmaxyx(w->win, oldrows, oldcols);
    getyx(w->win, y, x);
    if (y >= rows)
        y = rows - 1;
    if (x >= cols)
        x = cols - 1;
    if (wgetbkgrnd(w->win, &background) == ERR ||
        wattr_get(w->win, &attrs, &pair, NULL) == ERR)
        return 1;
    /* Some libraries turn dupwin(pad) into an ordinary window. Construct a
     * genuine pad, copy every original cell, then let wresize handle growth
     * and native wide-edge repair. The temporary pad is outside live quotas. */
    replacement = newpad(oldrows, oldcols);
    if (!replacement) {
        zwarnnam(nam, "failed to allocate replacement pad");
        return 1;
    }
    wbkgrndset(replacement, &background);
    if (copywin(w->win, replacement, 0, 0, 0, 0, oldrows - 1, oldcols - 1, 0) == ERR ||
        wresize(replacement, rows, cols) == ERR ||
        wattr_set(replacement, attrs, pair, NULL) == ERR ||
        wmove(replacement, y, x) == ERR ||
        scrollok(replacement, (w->flags & ZCWF_SCROLL) != 0) == ERR) {
        delwin(replacement);
        return 1;
    }
    if (delwin(w->win) == ERR) {
        delwin(replacement);
        return 1;
    }
    zdraw_pad_cells = zdraw_pad_cells - w->pad_cells + cells;
    w->pad_cells = cells;
    w->win = replacement;
    return 0;
#else
    (void)nam;
    (void)args;
    return 2;
#endif
}

static int
zccmd_viewport(const char *nam, char **args)
{
#ifdef ZDRAW_PADS
    LinkNode node;
    ZCWin w;
    int pr, pc, sr, sc, rows, cols, maxrows, maxcols;
    if (zdraw_nonnegative(args[1], &pr) ||
        zdraw_nonnegative(args[2], &pc) ||
        zdraw_nonnegative(args[3], &sr) ||
        zdraw_nonnegative(args[4], &sc) ||
        zdraw_nonnegative(args[5], &rows) ||
        zdraw_nonnegative(args[6], &cols) || !rows || !cols) {
        zwarnnam(nam, "viewport expects decimal coordinates and positive dimensions");
        return 1;
    }
    node = zdraw_validate_window(args[0], ZDRAW_USED);
    if (!node) {
        zwarnnam(nam, "%s: %s", zdraw_strerror(zc_errno), args[0]);
        return 1;
    }
    w = (ZCWin)getdata(node);
    if (!(w->flags & ZCWF_PAD)) {
        zwarnnam(nam, "viewport requires a pad: %s", args[0]);
        return 1;
    }
    getmaxyx(w->win, maxrows, maxcols);
    if (pr >= maxrows || pc >= maxcols ||
        rows > maxrows - pr || cols > maxcols - pc) {
        zwarnnam(nam, "viewport does not fit inside the pad");
        return 1;
    }
    getmaxyx(stdscr, maxrows, maxcols);
    if (sr >= maxrows || sc >= maxcols ||
        rows > maxrows - sr || cols > maxcols - sc) {
        zwarnnam(nam, "viewport does not fit inside the screen");
        return 1;
    }
    /* Fully contribute these rows even after another surface covered them.
     * This changes dirty markers, never retained text or drawing state. */
    if (touchline(w->win, pr, rows) == ERR)
        return 1;
    return pnoutrefresh(w->win, pr, pc, sr, sc,
                        sr + rows - 1, sc + cols - 1) == ERR;
#else
    (void)nam;
    (void)args;
    return 2;
#endif
}

static int
zccmd_stage(const char *nam, char **args)
{
    char **arg;
    LinkNode node;
    ZCWin w;
    /* Validate the whole list before changing the virtual screen. */
    for (arg = args; *arg; arg++) {
        node = zdraw_validate_window(*arg, ZDRAW_USED);
        if (!node) {
            zwarnnam(nam, "%s: %s", zdraw_strerror(zc_errno), *arg);
            return 1;
        }
        if (((ZCWin)getdata(node))->flags & ZCWF_PAD) {
            zwarnnam(nam, "use viewport to stage a pad: %s", *arg);
            return 1;
        }
    }
    for (arg = args; *arg; arg++) {
        w = (ZCWin)getdata(zdraw_getwindowbyname(*arg));
        if (touchwin(w->win) == ERR || wnoutrefresh(w->win) == ERR)
            return 1;
    }
    return 0;
}

static int
zccmd_present(UNUSED(const char *nam), UNUSED(char **args))
{
#ifdef ZDRAW_QUERIES
    int result;
    if (zdraw_presenting || zdraw_sync_reset())
        return 1;
    if (zdraw_sync_on) {
        /* No shell callback or input wait is allowed inside the wire frame.
         * Queue Zsh traps until the reset has been attempted, including errors. */
        queue_signals();
        zdraw_presenting = 1;
        result = fflush(stdout) == EOF;
        if (!result) {
            /* Even a failed flush can have enabled the peer: always reset. */
            zdraw_sync_applied = 1;
            result = fputs("\033[?2026h", stdout) == EOF || fflush(stdout) == EOF;
            if (!result)
                result = doupdate() == ERR;
            if (fflush(stdout) == EOF)
                result = 1;
            if (zdraw_sync_reset())
                result = 1;
        }
        zdraw_presenting = 0;
        unqueue_signals();
        return result;
    }
#endif
    return doupdate() == ERR;
}

static int
zdraw_sync_reset(void)
{
#ifdef ZDRAW_QUERIES
    if (zdraw_sync_applied) {
        if (fputs("\033[?2026l", stdout) == EOF || fflush(stdout) == EOF)
            return 1;
        zdraw_sync_applied = 0;
    }
#endif
    return 0;
}

static int
zccmd_sync(const char *nam, char **args)
{
#ifdef ZDRAW_QUERIES
    if (zdraw_presenting)
        return 1;
    if (!strcmp(args[0], "off")) {
        zdraw_sync_on = 0;
        return zdraw_sync_reset();
    }
    if (strcmp(args[0], "on"))
        return 1;
    if (zdraw_sync_on)
        return 0;
    if (zdraw_query_reports[2] != 2) {
        zwarnnam(nam, "sync needs an observed reset synchronized_output mode");
        return 2;
    }
    if (!isatty(0) || !isatty(1) || zdraw_sync_reset())
        return 1;
    zdraw_sync_on = 1;
    return 0;
#else
    (void)nam;
    (void)args;
    return 2;
#endif
}

/* Decode the same printable characters for measurement and styled drawing.
 * Use the system width consumed by curses, not Zsh's optional width table. */
static int
zdraw_text_next(char **text, int wide, convchar_t *wc, int *width)
{
    unsigned char ch = (unsigned char)**text;
    /* Zsh's decoder already returns ASCII without changing its shift state.
     * Printable ASCII has one native column in both supported text paths. */
    if (ch >= 32 && ch <= 126) {
        (*text)++;
        *wc = ch;
        *width = 1;
        return 0;
    }
#ifdef MULTIBYTE_SUPPORT
    if (wide) {
	int len = mb_metacharlenconv(*text, wc);
	if (len <= 0 || *wc == WEOF || !*wc || !iswprint(*wc) ||
	    (!isset(MULTIBYTE) && *wc > 127))
	    return 1;
	*width = wcwidth(*wc);
	if (*width < 0)
	    return 1;
	*text += len;
	return 0;
    }
#else
    (void)wide;
#endif
    ch = (unsigned char)*(*text)++;
    if (ch == Meta)
	ch = (unsigned char)*(*text)++ ^ 32;
    if (ch >= 128)
	return 2;
    if (ch < 32 || ch > 126)
	return 1;
    *wc = ch;
    *width = 1;
    return 0;
}

#ifdef ZDRAW_WIDE_SPANS
/* A bounded, invocation-local cache for single-scalar cells. Repeated spaces,
 * digits and table glyphs need one public curses round-trip per style/pair.
 * Combining sequences always take the complete representability check. */
#define ZDRAW_SPAN_CACHE_SIZE 64
struct zdraw_span_cache {
    wchar_t scalar;
    chtype attrs;
    short pair;
    cchar_t cell;
};

static int
zdraw_span_cell(cchar_t *cell, const wchar_t *group, chtype attrs, short pair,
                struct zdraw_span_cache *cache, int validate)
{
    struct zdraw_span_cache *entry = NULL;
    if (group[0] && !group[1]) {
        entry = cache + (unsigned int)group[0] % ZDRAW_SPAN_CACHE_SIZE;
        if (entry->scalar == group[0] && entry->attrs == attrs && entry->pair == pair) {
            *cell = entry->cell;
            return 0;
        }
    }
    /* setcchar may silently discard excess combining characters. */
    if (setcchar(cell, group, attrs, pair, NULL) == ERR ||
        (validate && (size_t)getcchar(cell, NULL, NULL, NULL, NULL) != wcslen(group) + 1))
        return 1;
    if (entry) {
        entry->scalar = group[0];
        entry->attrs = attrs;
        entry->pair = pair;
        entry->cell = *cell;
    }
    return 0;
}
#endif

#ifdef ZDRAW_GRAPHEME
/* Suppress UAX breaks before zero-width scalars: never split a native cell. */
static int
zdraw_text_break(struct zdraw_grapheme_state *state, convchar_t wc, int width)
{
    return zdraw_grapheme_break(state, (unsigned int)wc) && width != 0;
}
#endif

#ifdef ZDRAW_SAFE_GRAPHEME
struct zdraw_safe_group {
    wchar_t text[CCHARW_MAX + 1];
    int length, width;
    unsigned int ascii_checked[3];
};

/* Public curses round-trip, including exact text; never access cchar_t fields. */
static int
zdraw_safe_cell(cchar_t *cell, struct zdraw_safe_group *group,
                attr_t attrs, short pair)
{
    wchar_t text[CCHARW_MAX + 1];
    attr_t actual_attrs;
    short actual_pair;
    if (!group->length) return 0;
    group->text[group->length] = 0;
    if (setcchar(cell, group->text, attrs, pair, NULL) == ERR ||
        getcchar(cell, NULL, NULL, NULL, NULL) != group->length + 1 ||
        getcchar(cell, text, &actual_attrs, &actual_pair, NULL) == ERR ||
        wcscmp(text, group->text))
        return 2;
    return 0;
}

/* Validate native storage in headless queries, including discarded suffixes. */
static int
zdraw_safe_scalar(struct zdraw_safe_group *group, convchar_t wc, int width)
{
    cchar_t cell;
    if (width || !wc) {
        /* Headless validation uses the same attributes/pair throughout one
         * query. Reuse the exact round-trip check for repeated ASCII bases;
         * any combining suffix still requires its own complete check. */
        if (group->length == 1 && group->text[0] >= 32 && group->text[0] <= 126) {
            unsigned int index = (unsigned int)group->text[0] - 32;
            unsigned int bit = 1U << (index % 32);
            if (!(group->ascii_checked[index / 32] & bit)) {
                if (zdraw_safe_cell(&cell, group, A_NORMAL, 0)) return 2;
                group->ascii_checked[index / 32] |= bit;
            }
        } else if (zdraw_safe_cell(&cell, group, A_NORMAL, 0)) return 2;
        group->length = 0;
    }
    if (!wc) return 0;
    if (group->length == CCHARW_MAX) return 2;
    group->text[group->length++] = (wchar_t)wc;
    return 0;
}
#endif

#if defined(ZDRAW_WIDE_SPANS) || defined(HAVE_WADDCHNSTR) || defined(HAVE_WCHGAT)
/* A span has a complete style, independent of the window's current style. */
struct zdraw_span {
    chtype attrs;
    char *color;
    int first, count;
};


static int
zdraw_span_style(char *style, struct zdraw_span *span)
{
    char *token = dupstring(style), *next, *slash;
    struct zdraw_namenumberpair *attr;
    span->attrs = A_NORMAL;
    span->color = NULL;
    if (!*token)
	return 0;
    do {
	next = strchr(token, ',');
	if (next)
	    *next++ = '\0';
	if ((slash = strchr(token, '/'))) {
	    if (span->color)
		return 1;
	    *slash = '\0';
	    if (zdraw_color(token) == -2 || zdraw_color(slash + 1) == -2)
		return 1;
	    *slash = '/';
	    span->color = token;
	} else {
	    attr = zdraw_attrget(NULL, token);
	    if (!attr)
		return 1;
	    span->attrs |= attr->number;
	}
	token = next;
    } while (token);
    return 0;
}
#endif

#if defined(ZDRAW_WIDE_SPANS) || defined(HAVE_WADDCHNSTR)
#ifdef ZDRAW_WIDE_SPANS
typedef cchar_t ZDrawCell;
#else
typedef chtype ZDrawCell;
#endif

struct zdraw_row {
    ZDrawCell *cells;
    int *widths;
    int count, width;
};

struct zdraw_prepared {
    struct hashnode node;
    struct zdraw_row row;
    char *locale;
    int multibyte;
    size_t bytes;
    zlong draws;
};

/* Session-scoped cells contain session-scoped color-pair IDs. */
#define ZDRAW_PREPARED_LIMIT ((size_t)16 * 1024 * 1024)
static HashTable zdraw_prepared_rows;
static size_t zdraw_prepared_bytes;
static zlong zdraw_prepared_created, zdraw_prepared_draws;

static void
zdraw_free_prepared(HashNode node)
{
    struct zdraw_prepared *p = (struct zdraw_prepared *)node;
    zsfree(p->node.nam);
    zsfree(p->locale);
    zfree(p->row.cells, (size_t)p->row.count * sizeof(*p->row.cells));
    zfree(p->row.widths, (size_t)p->row.count * sizeof(*p->row.widths));
    zdraw_prepared_bytes -= p->bytes;
    zfree(p, sizeof(*p));
}

static const char *
zdraw_ctype_locale(void)
{
#ifdef HAVE_SETLOCALE
    const char *locale = setlocale(LC_CTYPE, NULL);
    return locale ? locale : "";
#else
    return "";
#endif
}

#ifdef ZDRAW_SAFE_GRAPHEME
/* Stream across spans, staging native cells until their complete grapheme fits.
 * A single grapheme may contain several native cells, all using its first style. */
static int
zdraw_compile_safe_spans(const char *nam, char **args, int cols, int clip,
                         struct zdraw_row *row)
{
    int nargs = arrlen(args), nspans = nargs / 2, i, j, result;
    int cw, boundary, keeping = 1, cluster_first = 0, cluster_width = 0;
    int cluster_style = 0, have_base = 0, *owners;
    size_t bytes = 0;
    struct zdraw_span *styles;
    short *pairs;
    struct zdraw_grapheme_state state = {0};
    struct zdraw_safe_group group = {{0}, 0, 0};
    convchar_t wc;
    char *str, *p;
    cchar_t cell;
    if (nargs < 2 || nargs % 2 || nspans > ZDRAW_SAFE_SPANS) return 1;
    styles = zhalloc((size_t)nspans * sizeof(*styles));
    pairs = zhalloc((size_t)nspans * sizeof(*pairs));
    for (i = 0; i < nspans; i++) {
        if (zdraw_span_style(args[2*i], styles + i)) return 1;
        pairs[i] = -1;
        for (p = args[2*i+1]; *p; p++) {
            if (++bytes > ZDRAW_SAFE_BYTES) return 1;
            if (*p == Meta) p++;
        }
    }
    if ((size_t)cols > (size_t)-1 / sizeof(*row->cells) ||
        (size_t)cols > (size_t)-1 / sizeof(*owners)) return 1;
    row->cells = zhalloc((size_t)(cols ? cols : 1) * sizeof(*row->cells));
    row->widths = zhalloc((size_t)(cols ? cols : 1) * sizeof(*row->widths));
    owners = zhalloc((size_t)(cols ? cols : 1) * sizeof(*owners));
    row->count = row->width = 0;
    MB_METACHARINIT();
    for (i = 0; i <= nspans; i++) {
        str = i < nspans ? args[2*i+1] : "";
        do {
            wc = 0; cw = 0;
            if (*str) {
                result = zdraw_text_next(&str, 1, &wc, &cw);
                if (result || (!cw && !have_base)) return 1;
            } else if (i < nspans) break;
            boundary = !wc || zdraw_text_break(&state, wc, cw);
            if ((cw || !wc) && group.length) {
                if (zdraw_safe_cell(&cell, &group, styles[cluster_style].attrs, 0))
                    return 2;
                if (keeping && cluster_width <= cols - row->width) {
                    row->cells[row->count] = cell;
                    row->widths[row->count] = group.width;
                    owners[row->count++] = cluster_style;
                }
                group.length = 0;
            }
            if (boundary) {
                if (keeping) {
                    if (cluster_width > cols - row->width) {
                        if (!clip) return 1;
                        row->count = cluster_first;
                        keeping = 0;
                    } else row->width += cluster_width;
                }
                cluster_first = row->count;
                cluster_width = 0;
                cluster_style = i;
            }
            if (!wc) break;
            if (cluster_width > INT_MAX - cw) return 1;
            cluster_width += cw;
            if (cw) { have_base = 1; group.width = cw; }
            if (group.length == CCHARW_MAX) return 2;
            group.text[group.length++] = (wchar_t)wc;
        } while (*str);
    }
    /* Allocation follows validation of every style and scalar, even offscreen. */
    for (j = 0; j < row->count; j++) {
        attr_t attrs;
        short pair;
        i = owners[j];
        if (pairs[i] < 0) {
            pairs[i] = 0;
            if (styles[i].color) {
                Colorpairnode color = zdraw_colorget(nam, styles[i].color);
                if (!color) return 1;
                pairs[i] = color->colorpair;
            }
        }
        if (getcchar(row->cells + j, group.text, &attrs, &pair, NULL) == ERR)
            return 1;
        group.length = wcslen(group.text);
        if (zdraw_safe_cell(row->cells + j, &group, styles[i].attrs, pairs[i]))
            return 2;
    }
    return 0;
}
#endif

/* Shared preflight for transient spans and persistent prepared rows. */
static int
zdraw_compile_spans(const char *nam, char **args, int cols, int clip,
                     struct zdraw_row *row)
{
    struct zdraw_span *spans, *span;
    Colorpairnode pair;
    int nargs = arrlen(args), nspans = nargs / 2;
    int i, j, count = 0, width = 0, result, cw, retaining = 1;
    convchar_t wc;
    short cp;
    char *str;
    ZDrawCell *cells;
    int *widths;
#ifdef ZDRAW_WIDE_SPANS
    cchar_t discarded;
    struct zdraw_span_cache cache[ZDRAW_SPAN_CACHE_SIZE] = {{0}};
    int keep_group;
    wchar_t **groups, *out, *group;
    size_t len;
#endif
    if (nargs < 2 || nargs % 2) {
        zwarnnam(nam, "spans: expected style/text pairs");
        return 1;
    }
    if ((size_t)nspans > (size_t)-1 / sizeof(*spans) ||
        (size_t)cols > (size_t)-1 / sizeof(*cells) ||
        (size_t)cols > (size_t)-1 / sizeof(*widths))
        return 1;
    spans = zhalloc((size_t)nspans * sizeof(*spans));
    cells = zhalloc((size_t)(cols ? cols : 1) * sizeof(*cells));
    widths = zhalloc((size_t)(cols ? cols : 1) * sizeof(*widths));
#ifdef ZDRAW_WIDE_SPANS
    if ((size_t)cols > (size_t)-1 / sizeof(*groups))
        return 1;
    groups = zhalloc((size_t)(cols ? cols : 1) * sizeof(*groups));
#endif
    for (i = 0; i < nspans; i++) {
	span = spans + i;
	if (zdraw_span_style(args[2*i], span)) {
	    zwarnnam(nam, "spans: invalid style: %s", args[2*i]);
	    return 1;
	}
	span->first = count;
	str = args[1 + 2*i];
#ifdef ZDRAW_WIDE_SPANS
	/* Room for decoded characters and a terminator after each group. */
	len = strlen(str);
	if (len > (size_t)-1 / (2 * sizeof(wchar_t)) - 1)
	    return 1;
	out = zhalloc(2 * (len + 1) * sizeof(wchar_t));
	group = NULL;
	keep_group = 0;
	MB_METACHARINIT();
	while (*str) {
	    result = zdraw_text_next(&str, 1, &wc, &cw);
	    if (result || (!cw && !group))
		goto badtext;
	    if (cw) {
		if (group) {
		    *out++ = L'\0';
		    if (zdraw_span_cell(keep_group ? cells + count - 1 : &discarded,
				  group, span->attrs, 0, cache, 1))
			goto badtext;
		}
		if (cw > cols - width) {
		    if (!clip)
			goto badtext;
		    retaining = 0;
		}
		group = out;
		keep_group = retaining;
		if (keep_group) {
		    widths[count] = cw;
		    groups[count++] = group;
		    width += cw;
		}
	    }
	    *out++ = (wchar_t)wc;
	}
	*out = L'\0';
	if (group && zdraw_span_cell(keep_group ? cells + count - 1 : &discarded,
				     group, span->attrs, 0, cache, 1))
	    goto badtext;
#else
	while (*str) {
	    result = zdraw_text_next(&str, 0, &wc, &cw);
	    if (result == 2) {
		zwarnnam(nam, "spans: non-ASCII text requires wide span support");
		return 2;
	    }
	    if (result)
		goto badtext;
	    if (width == cols) {
		if (!clip)
		    goto badtext;
		retaining = 0;
	    }
	    if (retaining) {
		widths[count] = 1;
		cells[count++] = (chtype)wc | span->attrs;
		width++;
	    }
	}
#endif
	span->count = count - span->first;
    }
    /* Colors share the existing cache. On failure, successfully allocated
     * pairs remain valid, but no cell has yet been written. */
    for (i = 0; i < nspans; i++) {
	span = spans + i;
	if (!span->count)
	    continue;
	cp = 0;
	if (span->color) {
	    pair = zdraw_colorget(nam, span->color);
	    if (!pair) {
		zwarnnam(nam, "spans: cannot allocate color pair: %s", span->color);
		return 1;
	    }
	    cp = pair->colorpair;
	}
#ifdef ZDRAW_WIDE_SPANS
        /* Preflight already constructed and checked these exact pair-0 cells. */
        if (!cp)
            continue;
#endif
	for (j = span->first; j < span->first + span->count; j++) {
#ifdef ZDRAW_WIDE_SPANS
            /* Every group passed representability preflight before colors
             * were allocated. Recoloring need only check setcchar's status. */
	    if (zdraw_span_cell(cells + j, groups[j], span->attrs, cp, cache, 0))
		return 1;
#else
	    if (PAIR_NUMBER(COLOR_PAIR(cp)) != cp) {
		zwarnnam(nam, "spans: color pair exceeds packed-attribute limit");
		return 1;
	    }
	    cells[j] |= COLOR_PAIR(cp);
#endif
	}
    }
    row->cells = cells;
    row->widths = widths;
    row->count = count;
    row->width = width;
    return 0;
badtext:
    zwarnnam(nam, "spans: text must be printable, representable and fit on the row");
    return 1;
}

/* Write identical rows under one saved cursor/background/style scope. */
static int
zdraw_write_rows(WINDOW *win, int row, int col, ZDrawCell *cells, int count,
                 int rows)
{
    int y, x, result, i;
#ifdef ZDRAW_WIDE_SPANS
    cchar_t saved_bg, neutral_bg;
    attr_t saved_attrs;
    short saved_pair;
#endif
    if (!count)
	return 0;
    getyx(win, y, x);
    if (wmove(win, row, col) == ERR)
	return 1;
#ifdef ZDRAW_WIDE_SPANS
    /* Wide array writes can merge window/background attributes and replace
     * spaces. Neutralize both temporarily to give every span a complete style. */
    if (wgetbkgrnd(win, &saved_bg) == ERR ||
	wattr_get(win, &saved_attrs, &saved_pair, NULL) == ERR ||
	setcchar(&neutral_bg, L" ", A_NORMAL, 0, NULL) == ERR) {
	(void)wmove(win, y, x);
	return 1;
    }
    wbkgrndset(win, &neutral_bg);
    result = wattr_set(win, A_NORMAL, 0, NULL);
#else
    result = OK;
#endif
    for (i = 0; i < rows && result != ERR; i++) {
        if (i && wmove(win, row + i, col) == ERR) {
            result = ERR;
            break;
        }
#ifdef ZDRAW_WIDE_SPANS
        result = wadd_wchnstr(win, cells, count);
#else
        result = waddchnstr(win, cells, count);
#endif
    }
#ifdef ZDRAW_WIDE_SPANS
    wbkgrndset(win, &saved_bg);
    if (wattr_set(win, saved_attrs, saved_pair, NULL) == ERR)
	result = ERR;
#endif
    if (wmove(win, y, x) == ERR)
	return 1;
    return result == ERR;
}

static int
zdraw_write_row(WINDOW *win, int row, int col, ZDrawCell *cells, int count)
{
    return zdraw_write_rows(win, row, col, cells, count, 1);
}

static int
zdraw_row_target(const char *nam, char **args, WINDOW **win,
                  int *row, int *col, int *cols)
{
    LinkNode node;
    int rows;
    if (zdraw_nonnegative(args[1], row) || zdraw_nonnegative(args[2], col)) {
        zwarnnam(nam, "expected nonnegative decimal coordinates");
        return 1;
    }
    node = zdraw_validate_window(args[0], ZDRAW_USED);
    if (!node) {
        zwarnnam(nam, "%s: %s", zdraw_strerror(zc_errno), args[0]);
        return 1;
    }
    *win = ((ZCWin)getdata(node))->win;
    getmaxyx(*win, rows, *cols);
    if (*row >= rows || *col >= *cols) {
        zwarnnam(nam, "coordinates outside window");
        return 1;
    }
    *cols -= *col;
    return 0;
}
#endif

static int
zdraw_drawspans(const char *nam, char **args, int clip)
{
#if defined(ZDRAW_WIDE_SPANS) || defined(HAVE_WADDCHNSTR)
    WINDOW *win;
    struct zdraw_row data;
    int row, col, cols, budget, result, policy = 0;
    char **spans = args + (clip ? 4 : 3);
    if (*spans && !strncmp(*spans, "policy=", 7)) {
        result = zdraw_text_policy(nam, *spans + 7, "", &policy);
        if (result) return result;
        if (policy == 1) return 2; /* Boundary-only query policy is not drawing. */
        spans++;
    }
    if (zdraw_row_target(nam, args, &win, &row, &col, &cols))
        return 1;
    if (clip) {
        if (zdraw_nonnegative(args[3], &budget)) {
            zwarnnam(nam, "spans: expected nonnegative decimal budget");
            return 1;
        }
        if (budget < cols)
            cols = budget;
    }
#ifdef ZDRAW_SAFE_GRAPHEME
    if (policy == 2)
        result = zdraw_compile_safe_spans(nam, spans, cols, clip, &data);
    else
#endif
        result = zdraw_compile_spans(nam, spans, cols, clip, &data);
    if (result)
        return result;
    return zdraw_write_row(win, row, col, data.cells, data.count);
#else
    (void)nam;
    (void)args;
    (void)clip;
    return 2;
#endif
}

/* Explicit safe strings are single-row writes with checked cursor advancement.
 * The inherited string operation, including wrapping and controls, is unchanged. */
static int
zdraw_policy_string(const char *nam, char **args)
{
    int policy, result;
    char *name = args[2];
    if (!strncmp(name, "policy=", 7)) name += 7;
    result = zdraw_text_policy(nam, name, args[1], &policy);
    if (result) return result;
    if (!policy) {
        char *legacy[] = {args[0], args[1], NULL};
        return zccmd_string(nam, legacy);
    }
    if (policy != 2) return 2;
#ifdef ZDRAW_SAFE_GRAPHEME
    {
        LinkNode node = zdraw_validate_window(args[0], ZDRAW_USED);
        WINDOW *win;
        struct zdraw_row data;
        struct zdraw_safe_group group;
        attr_t attrs, ignored_attrs;
        short pair, ignored_pair;
        int y, x, rows, cols, end_y, end_x, i;
        char *spans[] = {"", args[1], NULL};
        if (!node) return 1;
        win = ((ZCWin)getdata(node))->win;
        getyx(win, y, x); getmaxyx(win, rows, cols);
        result = zdraw_compile_safe_spans(nam, spans, cols - x, 0, &data);
        if (result) return result;
        end_y = y; end_x = x + data.width;
        if (end_x == cols) { end_y++; end_x = 0; }
        /* Never implicitly scroll or wrap a cluster onto another row. */
        if (end_y >= rows) return 1;
        if (wattr_get(win, &attrs, &pair, NULL) == ERR) return 1;
        for (i = 0; i < data.count; i++) {
            if (getcchar(data.cells + i, group.text, &ignored_attrs,
                         &ignored_pair, NULL) == ERR) return 1;
            group.length = wcslen(group.text);
            if (zdraw_safe_cell(data.cells + i, &group, attrs, pair)) return 2;
        }
        result = zdraw_write_row(win, y, x, data.cells, data.count);
        if (result) return result;
        return wmove(win, end_y, end_x) == ERR;
    }
#else
    return 2;
#endif
}

static int
zccmd_fill(const char *nam, char **args)
{
#if defined(ZDRAW_WIDE_SPANS) || defined(HAVE_WADDCHNSTR)
    WINDOW *win;
    struct zdraw_row tile;
    ZDrawCell *cells;
    int row, col, available, rows, cols, maxrows, maxcols, i, result;

    if (zdraw_row_target(nam, args, &win, &row, &col, &available))
        return 1;
    if (zdraw_nonnegative(args[3], &rows) ||
        zdraw_nonnegative(args[4], &cols) || !rows || !cols) {
        zwarnnam(nam, "fill expects positive decimal dimensions");
        return 1;
    }
    getmaxyx(win, maxrows, maxcols);
    (void)maxcols;
    if (rows > maxrows - row || cols > available ||
        (size_t)cols > (size_t)-1 / sizeof(*cells)) {
        zwarnnam(nam, "fill rectangle does not fit inside the window");
        return 1;
    }
    /* A tile is exactly one single-column complex cell. The compiler checks
     * its entire text and style before allocating a shared color pair. */
    result = zdraw_compile_spans(nam, args + 5, 1, 0, &tile);
    if (result)
        return result;
    if (tile.count != 1 || tile.width != 1) {
        zwarnnam(nam, "fill expects one printable single-column cell");
        return 1;
    }
    cells = (ZDrawCell *)zhalloc((size_t)cols * sizeof(*cells));
    for (i = 0; i < cols; i++)
        cells[i] = tile.cells[0];
    return zdraw_write_rows(win, row, col, cells, cols, rows);
#else
    (void)nam;
    (void)args;
    return 2;
#endif
}

static int
zccmd_restyle(const char *nam, char **args)
{
#ifdef HAVE_WCHGAT
    LinkNode node;
    WINDOW *win;
    struct zdraw_span style;
    Colorpairnode pair;
    short cp = 0;
    int row, col, rows, cols, maxrows, maxcols, y, x, i, result = 0;

    if (zdraw_nonnegative(args[1], &row) ||
        zdraw_nonnegative(args[2], &col) ||
        zdraw_nonnegative(args[3], &rows) ||
        zdraw_nonnegative(args[4], &cols) || !rows || !cols) {
        zwarnnam(nam, "restyle expects decimal coordinates and positive dimensions");
        return 1;
    }
    node = zdraw_validate_window(args[0], ZDRAW_USED);
    if (!node) {
        zwarnnam(nam, "%s: %s", zdraw_strerror(zc_errno), args[0]);
        return 1;
    }
    win = ((ZCWin)getdata(node))->win;
    getmaxyx(win, maxrows, maxcols);
    if (row >= maxrows || col >= maxcols ||
        rows > maxrows - row || cols > maxcols - col) {
        zwarnnam(nam, "restyle rectangle does not fit inside the window");
        return 1;
    }
    if (zdraw_span_style(args[5], &style)) {
        zwarnnam(nam, "restyle: invalid style: %s", args[5]);
        return 1;
    }
    if (style.color) {
        pair = zdraw_colorget(nam, style.color);
        if (!pair) {
            zwarnnam(nam, "restyle: cannot allocate color pair: %s", style.color);
            return 1;
        }
        cp = pair->colorpair;
    }
#if defined(NCURSES_VERSION) && !defined(NCURSES_EXT_COLORS)
    /* Older ncurses ABIs pack even the dedicated short pair argument. */
    if (PAIR_NUMBER(COLOR_PAIR(cp)) != cp) {
        zwarnnam(nam, "restyle: color pair exceeds packed-attribute limit");
        return 1;
    }
#endif
    getyx(win, y, x);
    for (i = 0; i < rows; i++) {
        if (wmove(win, row + i, col) == ERR ||
            wchgat(win, cols, (attr_t)style.attrs, cp, NULL) == ERR) {
            result = 1;
            break;
        }
    }
    /* wchgat leaves text, current attributes and background alone. Always
     * attempt cursor restoration, including after a partial update failure. */
    if (wmove(win, y, x) == ERR)
        result = 1;
    return result;
#else
    (void)nam;
    (void)args;
    return 2;
#endif
}

/* Keep temporary storage proportional to the requested rectangle. */
#define ZDRAW_COPY_CELLS 65536

static int
zdraw_copy_region(const char *nam, char **args, int transparent)
{
#if defined(HAVE_COPYWIN) && defined(HAVE_NEWPAD)
    LinkNode node;
    WINDOW *source, *destination, *buffer;
    int sr, sc, dr, dc, rows, cols, maxrows, maxcols, result, y, x, start, blank;

    if (zdraw_nonnegative(args[1], &sr) ||
        zdraw_nonnegative(args[2], &sc) ||
        zdraw_nonnegative(args[4], &dr) ||
        zdraw_nonnegative(args[5], &dc) ||
        zdraw_nonnegative(args[6], &rows) ||
        zdraw_nonnegative(args[7], &cols) || !rows || !cols) {
        zwarnnam(nam, "copy expects decimal coordinates and positive dimensions");
        return 1;
    }
    node = zdraw_validate_window(args[0], ZDRAW_USED);
    if (!node) {
        zwarnnam(nam, "%s: %s", zdraw_strerror(zc_errno), args[0]);
        return 1;
    }
    source = ((ZCWin)getdata(node))->win;
    node = zdraw_validate_window(args[3], ZDRAW_USED);
    if (!node) {
        zwarnnam(nam, "%s: %s", zdraw_strerror(zc_errno), args[3]);
        return 1;
    }
    destination = ((ZCWin)getdata(node))->win;
    getmaxyx(source, maxrows, maxcols);
    if (sr >= maxrows || sc >= maxcols ||
        rows > maxrows - sr || cols > maxcols - sc) {
        zwarnnam(nam, "copy rectangle does not fit inside the source");
        return 1;
    }
    getmaxyx(destination, maxrows, maxcols);
    if (dr >= maxrows || dc >= maxcols ||
        rows > maxrows - dr || cols > maxcols - dc) {
        zwarnnam(nam, "copy rectangle does not fit inside the destination");
        return 1;
    }
    if (rows > ZDRAW_COPY_CELLS / cols) {
        zwarnnam(nam, "copy rectangle exceeds the temporary cell limit");
        return 1;
    }
    buffer = newpad(rows, cols);
    if (!buffer) {
        zwarnnam(nam, "could not allocate the copy rectangle");
        return 1;
    }
    /* Stage even when names differ: subwindows can share backing storage.
     * Opaque copies retain literal blanks and source styles without decoding
     * text or allocating pairs. Never refresh this private pad. */
    result = copywin(source, buffer, sr, sc, 0, 0, rows - 1, cols - 1, 0);
    if (result != ERR && !transparent)
        result = copywin(buffer, destination, 0, 0,
                         dr, dc, dr + rows - 1, dc + cols - 1, 0);
    else if (result != ERR) {
        /* Preserve styled spaces as opaque; only a plain pair-zero space is a
         * hole. Inspect the snapshot, then copy contiguous opaque cell runs. */
        for (y = 0; y < rows && result != ERR; y++) {
            start = -1;
            for (x = 0; x <= cols; x++) {
                blank = 1;
                if (x < cols) {
                    if (wmove(buffer, y, x) == ERR) { result = ERR; break; }
#if defined(HAVE_WIN_WCH) && defined(HAVE_GETCCHAR)
                    {
                        cchar_t cell;
                        wchar_t text[CCHARW_MAX];
                        attr_t attrs;
                        short pair;
                        if (win_wch(buffer, &cell) == ERR || getcchar(&cell, text, &attrs, &pair, NULL) == ERR) {
                            result = ERR; break;
                        }
                        blank = text[0] == L' ' && !text[1] && attrs == A_NORMAL && pair == 0;
                    }
#else
                    {
                        chtype cell = winch(buffer);
                        if (cell == (chtype)ERR) { result = ERR; break; }
                        blank = cell == (chtype)' ';
                    }
#endif
                }
                if (!blank && start < 0) start = x;
                if (blank && start >= 0) {
                    result = copywin(buffer, destination, y, start, dr + y, dc + start,
                                     dr + y, dc + x - 1, 0);
                    start = -1;
                    if (result == ERR) break;
                }
            }
        }
    }
    if (delwin(buffer) == ERR)
        result = ERR;
    return result == ERR;
#else
    (void)nam;
    (void)args;
    (void)transparent;
    return 2;
#endif
}

static int
zccmd_copy(const char *nam, char **args)
{
    return zdraw_copy_region(nam, args, 0);
}

static int
zccmd_overlay(const char *nam, char **args)
{
    return zdraw_copy_region(nam, args, 1);
}

static int
zccmd_prepare(const char *nam, char **args)
{
#if defined(ZDRAW_WIDE_SPANS) || defined(HAVE_WADDCHNSTR)
    struct zdraw_prepared *p;
    struct zdraw_row data;
    int nargs = arrlen(args), i, width = 0, cw, result, wide = 0;
    convchar_t wc;
    char *str;
    const char *locale = zdraw_ctype_locale();
    size_t bytes;
    if (!isident(args[0]) || strchr(args[0], '[') || nargs % 2 != 1) {
        zwarnnam(nam, "prepare expects an identifier and style/text pairs");
        return 1;
    }
    if (zdraw_prepared_rows && gethashnode2(zdraw_prepared_rows, args[0])) {
        zwarnnam(nam, "prepared row already exists: %s", args[0]);
        return 1;
    }
#ifdef ZDRAW_WIDE_SPANS
    wide = 1;
#endif
    /* Determine capacity without assuming a maximum system character width. */
    for (i = 2; i < nargs; i += 2) {
        str = args[i];
        MB_METACHARINIT();
        while (*str) {
            result = zdraw_text_next(&str, wide, &wc, &cw);
            if (result || width > INT_MAX - cw) {
                zwarnnam(nam, "prepare requires printable, representable text");
                return result == 2 ? 2 : 1;
            }
            width += cw;
        }
    }
    bytes = sizeof(*p) + strlen(args[0]) + 1 + strlen(locale) + 1;
    if (bytes > ZDRAW_PREPARED_LIMIT - zdraw_prepared_bytes ||
        (size_t)width > (ZDRAW_PREPARED_LIMIT - zdraw_prepared_bytes - bytes) /
                       (sizeof(ZDrawCell) + sizeof(int))) {
        zwarnnam(nam, "prepared rows exceed the session storage limit");
        return 1;
    }
    result = zdraw_compile_spans(nam, args + 1, width, 0, &data);
    if (result)
        return result;
    if (!zdraw_prepared_rows) {
        zdraw_prepared_rows = newhashtable(8, "zdraw_prepared_rows", NULL);
        zdraw_prepared_rows->hash = hasher;
        zdraw_prepared_rows->emptytable = emptyhashtable;
        zdraw_prepared_rows->cmpnodes = strcmp;
        zdraw_prepared_rows->addnode = addhashnode;
        zdraw_prepared_rows->getnode = gethashnode2;
        zdraw_prepared_rows->getnode2 = gethashnode2;
        zdraw_prepared_rows->removenode = removehashnode;
        zdraw_prepared_rows->freenode = zdraw_free_prepared;
    }
    p = (struct zdraw_prepared *)zshcalloc(sizeof(*p));
    p->row = data;
    p->row.cells = data.count ? zalloc((size_t)data.count * sizeof(*data.cells)) : NULL;
    p->row.widths = data.count ? zalloc((size_t)data.count * sizeof(*data.widths)) : NULL;
    if (data.count) {
        memcpy(p->row.cells, data.cells, (size_t)data.count * sizeof(*data.cells));
        memcpy(p->row.widths, data.widths, (size_t)data.count * sizeof(*data.widths));
    }
    p->locale = ztrdup(locale);
    p->multibyte = isset(MULTIBYTE);
    p->bytes = bytes + (size_t)data.count * (sizeof(*data.cells) + sizeof(*data.widths));
    zdraw_prepared_bytes += p->bytes;
    addhashnode(zdraw_prepared_rows, ztrdup(args[0]), p);
    if (zdraw_prepared_created < ZLONG_MAX)
        zdraw_prepared_created++;
    return 0;
#else
    (void)nam;
    (void)args;
    return 2;
#endif
}

static int
zccmd_draw(const char *nam, char **args)
{
#if defined(ZDRAW_WIDE_SPANS) || defined(HAVE_WADDCHNSTR)
    struct zdraw_prepared *p;
    WINDOW *win;
    int row, col, cols, budget, count = 0, width = 0, result;
    if (zdraw_row_target(nam, args, &win, &row, &col, &cols))
        return 1;
    p = zdraw_prepared_rows ?
        (struct zdraw_prepared *)gethashnode2(zdraw_prepared_rows, args[3]) : NULL;
    if (!p) {
        zwarnnam(nam, "unknown prepared row: %s", args[3]);
        return 1;
    }
    if (strcmp(p->locale, zdraw_ctype_locale()) || p->multibyte != isset(MULTIBYTE)) {
        zwarnnam(nam, "prepared row requires its original LC_CTYPE and MULTIBYTE setting");
        return 1;
    }
    if (args[4]) {
        if (zdraw_nonnegative(args[4], &budget)) {
            zwarnnam(nam, "draw expects a nonnegative decimal column budget");
            return 1;
        }
        if (budget < cols)
            cols = budget;
    } else if (p->row.width > cols) {
        zwarnnam(nam, "prepared row does not fit");
        return 1;
    }
    if (p->row.width <= cols)
        count = p->row.count;
    else
        while (count < p->row.count && p->row.widths[count] <= cols - width)
            width += p->row.widths[count++];
    result = zdraw_write_row(win, row, col, p->row.cells, count);
    if (!result) {
        if (p->draws < ZLONG_MAX) p->draws++;
        if (zdraw_prepared_draws < ZLONG_MAX) zdraw_prepared_draws++;
    }
    return result;
#else
    (void)nam;
    (void)args;
    return 2;
#endif
}

static int
zccmd_unprepare(const char *nam, char **args)
{
#if defined(ZDRAW_WIDE_SPANS) || defined(HAVE_WADDCHNSTR)
    HashNode node = zdraw_prepared_rows ? removehashnode(zdraw_prepared_rows, args[0]) : NULL;
    if (!node) {
        zwarnnam(nam, "unknown prepared row: %s", args[0]);
        return 1;
    }
    zdraw_free_prepared(node);
    return 0;
#else
    (void)nam;
    (void)args;
    return 2;
#endif
}

static int
zccmd_spans(const char *nam, char **args)
{
    return zdraw_drawspans(nam, args, 0);
}

static int
zccmd_spansclip(const char *nam, char **args)
{
    return zdraw_drawspans(nam, args, 1);
}

static int
zccmd_border(const char *nam, char **args)
{
    LinkNode node;
    ZCWin w;
    int i, nargs = 0;
#if defined(HAVE_SETCCHAR) && defined(HAVE_WBORDER_SET)
    cchar_t chars[8];
    const cchar_t *border[8];
    wchar_t glyph[2];
    wint_t wc;
    int len;
#else
    chtype border[8];
#endif

    while (args[nargs])
	nargs++;
    if (nargs != 1 && nargs != 9) {
	zwarnnam(nam, "border expects a window and either zero or eight characters");
	return 1;
    }

    node = zdraw_validate_window(args[0], ZDRAW_USED);
    if (node == NULL) {
	zwarnnam(nam, "%s: %s", zdraw_strerror(zc_errno), args[0]);
	return 1;
    }

    w = (ZCWin)getdata(node);

    if (nargs == 1)
	return wborder(w->win, 0, 0, 0, 0, 0, 0, 0, 0) != OK;

    /* Degenerate perimeters have overlapping edges and corners.  Keep the
     * legacy operation, but require unambiguous geometry for custom borders. */
    if (getmaxy(w->win) < 2 || getmaxx(w->win) < 2) {
	zwarnnam(nam, "custom border requires at least two rows and columns");
	return 1;
    }

    /* Prepare every glyph before drawing.  Empty selects the curses default;
     * a space is an explicit blank, not a request for the default. */
    for (i = 0; i < 8; i++) {
	char *arg = args[i+1];
	if (!*arg) {
	    border[i] = 0;
	    continue;
	}
#if defined(HAVE_SETCCHAR) && defined(HAVE_WBORDER_SET)
	mb_charinit();
	len = mb_metacharlenconv(arg, &wc);
	/* Use the system width used by curses, not Zsh's optional width table. */
	if (len <= 0 || wc == WEOF || arg[len] || !iswprint(wc) ||
	    wcwidth(wc) != 1) {
	    zwarnnam(nam, "border character %d must be one printable single-width character", i+1);
	    return 1;
	}
	glyph[0] = wc;
	glyph[1] = L'\0';
	if (setcchar(&chars[i], glyph, A_NORMAL, 0, NULL) == ERR)
	    return 1;
	border[i] = &chars[i];
#else
	if ((unsigned char)*arg >= 0x80) {
	    zwarnnam(nam, "wide border characters are not supported by this build");
	    return 2;
	}
	if (*arg < 0x20 || *arg > 0x7e || arg[1]) {
	    zwarnnam(nam, "border character %d must be one printable ASCII character", i+1);
	    return 1;
	}
	border[i] = (chtype)*arg;
#endif
    }

#if defined(HAVE_SETCCHAR) && defined(HAVE_WBORDER_SET)
    return wborder_set(w->win, border[0], border[1], border[2], border[3],
		       border[4], border[5], border[6], border[7]) != OK;
#else
    return wborder(w->win, border[0], border[1], border[2], border[3],
		   border[4], border[5], border[6], border[7]) != OK;
#endif
}


#ifdef ZDRAW_PASTE
static int
zdraw_paste_mode(int on)
{
    return fputs(on ? "\033[?2004h" : "\033[?2004l", stdout) == EOF ||
        fflush(stdout) == EOF;
}

static void
zdraw_paste_restore_tty(void)
{
    noraw();
    nl();
    cbreak();
    settyinfo(&zdraw_paste_tty_state);
    gettyinfo(&curses_tty_state);
}
#endif

static int
zccmd_paste(const char *nam, char **args)
{
#ifdef ZDRAW_PASTE
    int code;
    char *bound;
    if (!strcmp(args[0], "off")) {
        if (zdraw_paste_active) {
            zwarnnam(nam, "finish the active paste before disabling it");
            return 1;
        }
        if (!zdraw_paste_key)
            return 0;
        if (zdraw_paste_mode(0))
            return 1;
        if (define_key(NULL, zdraw_paste_key) == ERR) {
            zdraw_paste_mode(1);
            return 1;
        }
        zdraw_paste_restore_tty();
        zdraw_paste_key = 0;
        return 0;
    }
    if (strcmp(args[0], "on")) {
        zwarnnam(nam, "paste expects on or off");
        return 1;
    }
    if (zdraw_paste_key)
        return 0;
    if (!isatty(0) || !isatty(1) || key_defined("\033[200~")) {
        zwarnnam(nam, "paste needs terminal stdin/stdout and an unbound start delimiter");
        return 1;
    }
    for (code = KEY_MAX + 1; code < KEY_MAX + 257; code++) {
        bound = keybound(code, 0);
        if (!bound)
            break;
        free(bound);
    }
    if (code == KEY_MAX + 257 || define_key("\033[200~", code) == ERR)
        return 1;
    gettyinfo(&zdraw_paste_tty_state);
    if (raw() == ERR || nonl() == ERR) {
        zdraw_paste_restore_tty();
        define_key(NULL, code);
        return 1;
    }
    if (zdraw_paste_mode(1)) {
        zdraw_paste_mode(0);
        define_key(NULL, code);
        zdraw_paste_restore_tty();
        return 1;
    }
    zdraw_paste_key = code;
    gettyinfo(&curses_tty_state);
    return 0;
#else
    (void)nam; (void)args;
    return 2;
#endif
}

static int
zccmd_suspend(const char *nam, UNUSED(char **args))
{
#ifdef ZDRAW_SUSPEND
    if (zdraw_suspended)
        return 0;
#ifdef ZDRAW_PASTE
    if (zdraw_paste_active) {
        zwarnnam(nam, "finish the active paste before suspending");
        return 1;
    }
#endif
#ifdef ZDRAW_ENHANCED
    if (zdraw_key_used || zdraw_key_discard) {
        zwarnnam(nam, "finish the partial keyboard event before suspending");
        return 1;
    }
#endif
    if (def_prog_mode() == ERR)
        return 1;
    if (zdraw_sync_reset())
        return 1;
    gettyinfo(&curses_tty_state);
    if (zdraw_enhanced_pause())
        return 1;
#ifdef ZDRAW_PASTE
    if (zdraw_paste_key && zdraw_paste_mode(0)) {
        zdraw_paste_mode(1);
        zdraw_enhanced_resume();
        return 1;
    }
#endif
#ifdef NCURSES_MOUSE_VERSION
    zdraw_saved_mouse = (zdraw_flags & ZCF_MOUSE_ACTIVE) != 0;
    mousemask(0, NULL);
#endif
    if (!isendwin() && endwin() == ERR) {
        reset_prog_mode();
        zdraw_enhanced_resume();
#ifdef ZDRAW_PASTE
        if (zdraw_paste_key)
            zdraw_paste_mode(1);
#endif
#ifdef NCURSES_MOUSE_VERSION
        if (zdraw_saved_mouse)
            mousemask(zdraw_mouse_mask, NULL);
#endif
        return 1;
    }
    zdraw_query_cancel();
    settyinfo(&saved_tty_state);
    zdraw_suspended = 1;
    return 0;
#else
    (void)nam;
    return 2;
#endif
}

static int
zccmd_resume(UNUSED(const char *nam), UNUSED(char **args))
{
#ifdef ZDRAW_SUSPEND
    int result;
#ifdef HAVE_RESIZE_TERM
    int rows, cols;
#endif
    if (!zdraw_suspended)
        return zdraw_enhanced_resume();
#ifdef HAVE_TCSETPGRP
    /* A background continuation must leave shell mode intact until fg. */
    if (isatty(0) && tcgetpgrp(0) != getpgrp())
        return 1;
#endif
    if (reset_prog_mode() == ERR) {
        settyinfo(&saved_tty_state);
        return 1;
    }
    settyinfo(&curses_tty_state);
#ifdef HAVE_RESIZE_TERM
    if (!zdraw_terminal_size(&rows, &cols) && resize_term(rows, cols) == ERR) {
        endwin();
        settyinfo(&saved_tty_state);
        return 1;
    }
#endif
    /* Restore the retained virtual frame. Applications still own relayout. */
    clearok(curscr, TRUE);
    if (doupdate() == ERR) {
        endwin();
        settyinfo(&saved_tty_state);
        return 1;
    }
    zdraw_suspended = 0;
    /* Attempt every configured restoration even if one protocol write fails. */
    result = zdraw_enhanced_resume();
#ifdef NCURSES_MOUSE_VERSION
    if (zdraw_saved_mouse)
        mousemask(zdraw_mouse_mask, NULL);
#endif
#ifdef ZDRAW_PASTE
    if (zdraw_paste_key && zdraw_paste_mode(1))
        result = 1;
#endif
    return result;
#else
    return 2;
#endif
}

static int
zccmd_inputdelay(const char *nam, char **args)
{
#ifdef ZDRAW_INPUT_DELAY
    int delay;
    if (zdraw_nonnegative(args[0], &delay) || delay > 1000) {
        zwarnnam(nam, "inputdelay expects milliseconds from 0 through 1000");
        return 1;
    }
    if (zdraw_saved_escape_delay < 0)
        zdraw_saved_escape_delay = get_escdelay();
    return set_escdelay(delay) == ERR;
#else
    (void)nam; (void)args;
    return 2;
#endif
}

static int
zccmd_endwin(UNUSED(const char *nam), UNUSED(char **args))
{
    LinkNode stdscr_win = zdraw_getwindowbyname("stdscr");

    zdraw_tree_collect_retired();
    zdraw_raster_cleanup();
    if (stdscr_win) {
        zdraw_sync_reset();
#ifdef ZDRAW_QUERIES
        zdraw_sync_on = zdraw_sync_applied = zdraw_presenting = 0;
#endif
        zdraw_enhanced_cleanup();
        zdraw_query_cleanup();
#ifdef ZDRAW_PASTE
        if (zdraw_paste_key) {
            if (!zdraw_suspended)
                zdraw_paste_mode(0);
            define_key(NULL, zdraw_paste_key);
            noraw();
            nl();
        }
        if (zdraw_paste_active)
            flushinp();
        zdraw_paste_key = zdraw_paste_active = zdraw_paste_match = 0;
#endif
#ifdef ZDRAW_INPUT_DELAY
        if (zdraw_saved_escape_delay >= 0)
            set_escdelay(zdraw_saved_escape_delay);
        zdraw_saved_escape_delay = -1;
#endif
#ifdef NCURSES_VERSION
        if (zdraw_input_pad) {
            delwin(zdraw_input_pad);
            zdraw_input_pad = NULL;
        }
#endif
#if defined(ZDRAW_WIDE_SPANS) || defined(HAVE_WADDCHNSTR)
        if (zdraw_prepared_rows) {
            deletehashtable(zdraw_prepared_rows);
            zdraw_prepared_rows = NULL;
        }
        zdraw_prepared_created = zdraw_prepared_draws = 0;
#endif
	if (!zdraw_suspended)
            endwin();
        zdraw_suspended = 0;
	/* Restore TTY as it was before zdraw -i */
	settyinfo(&saved_tty_state);
	/*
	 * TODO: should I need the following?  Without it
	 * the screen stays messed up.  Presumably we are
	 * doing stuff with shttyinfo when we shouldn't really be.
	 */
	gettyinfo(&shttyinfo);
	freelinklist(zdraw_windows, (FreeFunc) zdraw_free_window);
	zdraw_windows = znewlinklist();
        zdraw_screen_end();
	if (zdraw_colorpairs) {
	    deletehashtable(zdraw_colorpairs);
	    zdraw_colorpairs = NULL;
	}
	next_cp = 0;
#ifdef NCURSES_MOUSE_VERSION
        zdraw_flags = 0;
#endif
        zdraw_event_rows = zdraw_event_cols = 0;
	zc_color_phase = 0;
	zc_has_colors = zc_color_started = zc_default_colors = 0;
	zc_can_change_color = 0;
	zc_truecolor = zc_truecolor_supported = 0;
	zc_rgb_min = -1;
    }
    return 0;
}


static int
zccmd_attr(const char *nam, char **args)
{
    LinkNode node;
    ZCWin w;
    char **attrs;
    int ret = 0;

    if (!args[0])
	return 1;

    node = zdraw_validate_window(args[0], ZDRAW_USED);
    if (node == NULL) {
	zwarnnam(nam, "%s: %s", zdraw_strerror(zc_errno), args[0]);
	return 1;
    }

    w = (ZCWin)getdata(node);

    for(attrs = args+1; *attrs; attrs++) {
	if (strchr(*attrs, '/')) {
	    Colorpairnode cpn;
	    if ((cpn = zdraw_colorget(nam, *attrs)) == NULL ||
		wcolor_set(w->win, cpn->colorpair, NULL) == ERR)
		ret = 1;
	} else {
	    char *ptr;
	    int onoff;
	    struct zdraw_namenumberpair *zca;

	    switch(*attrs[0]) {
	    case '-':
		onoff = ZDRAW_ATTROFF;
		ptr = (*attrs) + 1;
		break;
	    case '+':
		onoff = ZDRAW_ATTRON;
		ptr = (*attrs) + 1;
		break;
	    default:
		onoff = ZDRAW_ATTRON;
		ptr = *attrs;
		break;
	    }
	    if ((zca = zdraw_attrget(w->win, ptr)) == NULL) {
		zwarnnam(nam, "attribute `%s' not known", ptr);
		ret = 1;
	    } else {
		switch(onoff) {
		    case ZDRAW_ATTRON:
			if (wattron(w->win, zca->number) == ERR)
			    ret = 1;
			break;
		    case ZDRAW_ATTROFF:
			if (wattroff(w->win, zca->number) == ERR)
			    ret = 1;
			break;
		}
	    }
	}
    }
    return ret;
}


static int
zccmd_bg(const char *nam, char **args)
{
    LinkNode node;
    ZCWin w;
    char **attrs;
    int ret = 0;
#ifdef HAVE_SETCCHAR
    cchar_t cc;
    wchar_t wch[2] = { L' ', L'\0' };
    attr_t  bg_attrs = A_NORMAL;
    short   bg_cp = 0;
#else
    chtype ch = 0;
#endif

    if (!args[0])
	return 1;

    node = zdraw_validate_window(args[0], ZDRAW_USED);
    if (node == NULL) {
	zwarnnam(nam, "%s: %s", zdraw_strerror(zc_errno), args[0]);
	return 1;
    }

    w = (ZCWin)getdata(node);

    for (attrs = args+1; *attrs; attrs++) {
	if (strchr(*attrs, '/')) {
	    Colorpairnode cpn;
	    if ((cpn = zdraw_colorget(nam, *attrs)) == NULL)
		ret = 1;
	    else
#ifdef HAVE_SETCCHAR
		bg_cp = (short)cpn->colorpair;
	} else if (**attrs == '@') {
	    wch[0] = (wchar_t)(unsigned char)((*attrs)[1] == Meta
			    ? (*attrs)[2] ^ 32
			    : (*attrs)[1]);
#else
	    if (cpn->colorpair >= 256) {
		/* pretty unlikely, but... */
		zwarnnam(nam, "bg color pair %s has index (%d) too large (max 255)",
			 cpn->node.nam, cpn->colorpair);
		ret = 1;
	    } else {
		ch |= COLOR_PAIR(cpn->colorpair);
	    }
	} else if (**attrs == '@') {
	    ch |= (*attrs)[1] == Meta ? (*attrs)[2] ^ 32 : (*attrs)[1];
#endif
	} else {
	    char *ptr;
	    int onoff;
	    struct zdraw_namenumberpair *zca;

	    switch(*attrs[0]) {
	    case '-':
		onoff = ZDRAW_ATTROFF;
		ptr = (*attrs) + 1;
		break;
	    case '+':
		onoff = ZDRAW_ATTRON;
		ptr = (*attrs) + 1;
		break;
	    default:
		onoff = ZDRAW_ATTRON;
		ptr = *attrs;
		break;
	    }
	    if ((zca = zdraw_attrget(w->win, ptr)) == NULL) {
		zwarnnam(nam, "attribute `%s' not known", ptr);
		ret = 1;
	    } else {
#ifdef HAVE_SETCCHAR
		switch (onoff) {
		    case ZDRAW_ATTRON:
			bg_attrs |= zca->number;
			break;
		    case ZDRAW_ATTROFF:
			bg_attrs &= ~zca->number;
			break;
		}
	    }
	}
    }

    if (ret == 0) {
	if (setcchar(&cc, wch, bg_attrs, bg_cp, NULL) == ERR)
	    return 1;
	return wbkgrnd(w->win, &cc) != OK;
    }
#else
		switch(onoff) {
		    case ZDRAW_ATTRON:
			ch |= zca->number;
			break;
		    case ZDRAW_ATTROFF:
			ch &= ~zca->number;
			break;
		}
	    }
	}
    }

    if (ret == 0)
	return wbkgd(w->win, ch) != OK;
#endif
    return ret;
}


static int
zccmd_scroll(const char *nam, char **args)
{
    LinkNode node;
    ZCWin w;
    int ret = 0;

    node = zdraw_validate_window(args[0], ZDRAW_USED);
    if (node == NULL) {
	zwarnnam(nam, "%s: %s", zdraw_strerror(zc_errno), args[0]);
	return 1;
    }

    w = (ZCWin)getdata(node);

    if (!strcmp(args[1], "on")) {
	scrollok(w->win, TRUE);
	w->flags |= ZCWF_SCROLL;
    } else if (!strcmp(args[1], "off")) {
	scrollok(w->win, FALSE);
	w->flags &= ~ZCWF_SCROLL;
    } else {
	char *endptr;
	zlong sl = zstrtol(args[1], &endptr, 10);
	if (*endptr) {
	    zwarnnam(nam, "scroll requires `on', `off' or integer: %s",
		     args[1]);
	    return 1;
	}
	if (!(w->flags & ZCWF_SCROLL))
	    scrollok(w->win, TRUE);
	if (wscrl(w->win, (int)sl) == ERR)
	    ret = 1;
	if (!(w->flags & ZCWF_SCROLL))
	    scrollok(w->win, FALSE);
    }

    return ret;
}


/* Both interfaces consume the same curses queue and preserve its decoding
 * and timeout behavior. This helper never assigns shell parameters. */
struct zdraw_input {
    char *text;
    int key;
    zlong code;
};

static int
zdraw_read_input(const char *nam, WINDOW *win, int nargs, struct zdraw_input *input)
{
    int keypadnum = -1;
#ifdef HAVE_WGET_WCH
    int ret;
    wint_t wi;
    VARARR(char, instr, 2*MB_CUR_MAX+1);
#else
    int ci;
    char instr[3];
#endif
    keypad(win, nargs >= 3);

    if (nargs >= 4) {
#ifdef NCURSES_MOUSE_VERSION
	if (!(zdraw_flags & ZCF_MOUSE_ACTIVE) ||
	    (zdraw_flags & ZCF_MOUSE_MASK_CHANGED)) {
	    if (!mousemask(zdraw_mouse_mask, NULL)) {
		zwarnnam(nam, "current mouse mode is not supported");
		return 1;
	    }
	    zdraw_flags = (zdraw_flags & ~ZCF_MOUSE_MASK_CHANGED) |
		ZCF_MOUSE_ACTIVE;
	}
#else
	zwarnnam(nam, "mouse events are not supported");
	return 1;
#endif
    }
#ifdef NCURSES_MOUSE_VERSION
    else {
	if (zdraw_flags & ZCF_MOUSE_ACTIVE) {
	    mousemask((mmask_t)0, NULL);
	    zdraw_flags &= ~ZCF_MOUSE_ACTIVE;
	}
    }
#endif

    /*
     * Linux, OS X, FreeBSD documentation for wgetch() mentions:

       Programmers concerned about portability should be prepared for  either
       of  two cases: (a) signal receipt does not interrupt getch; (b) signal
       receipt interrupts getch and causes it to return ERR with errno set to
       EINTR.  Under the ncurses implementation, handled signals never inter-
       rupt getch.

     * Some observed behavior: wgetch() returns ERR with EINTR when a signal is
     * handled by the shell "trap" command mechanism. Observed that it returns
     * ERR twice, the second time without even attempting to repeat the
     * interrupted read. Third call will then begin reading again.
     *
     * Because of widespread of previous implementation that called wget*ch
     * possibly indefinitely many times after ERR/EINTR, and because of the
     * above observation, wget_wch call is repeated after each ERR/EINTR, but
     * errno is being reset (it wasn't) and the loop to all means should break.
     * Problem: the timeout may be waited twice.
     */
    errno = 0;

#ifdef HAVE_WGET_WCH
    while ((ret = wget_wch(win, &wi)) == ERR) {
	if (errno != EINTR || errflag || retflag || breaks || exit_pending)
	    break;
        errno = 0;
    }
    switch (ret) {
    case OK:
        input->code = (zlong)wi;
	ret = wctomb(instr, (wchar_t)wi);
	if (ret <= 0) {
	    return 1;
	} else {
	    (void)metafy(instr, ret, META_NOALLOC);
	}
	break;

    case KEY_CODE_YES:
	*instr = '\0';
	keypadnum = (int)wi;
	break;

    case ERR:
    default:
	return 1;
    }
#else
    while ((ci = wgetch(win)) == ERR) {
	if (errno != EINTR || errflag || retflag || breaks || exit_pending)
	    return 1;
        errno = 0;
    }
    input->code = ci;
    if (ci >= 256) {
	keypadnum = ci;
	*instr = '\0';
    } else {
	if (imeta(ci)) {
	    instr[0] = Meta;
	    instr[1] = (char)ci ^ 32;
	    instr[2] = '\0';
	} else {
	    instr[0] = (char)ci;
	    instr[1] = '\0';
	}
    }
#endif
    input->text = dupstring(instr);
    input->key = keypadnum;
    if (keypadnum >= 0)
        input->code = keypadnum;
    return 0;
}

static int
zccmd_input(const char *nam, char **args)
{
    LinkNode node;
    struct zdraw_input input;
    char *var, *instr;
    int nargs = arrlen(args), keypadnum;
    node = zdraw_validate_window(args[0], ZDRAW_USED);
    if (!node) {
        zwarnnam(nam, "%s: %s", zdraw_strerror(zc_errno), args[0]);
        return 1;
    }
    if (zdraw_read_input(nam, ((ZCWin)getdata(node))->win, nargs, &input))
        return 1;
    instr = input.text;
    keypadnum = input.key;
    if (args[1])
	var = args[1];
    else
	var = "REPLY";
    if (!setsparam(var, ztrdup(instr)))
	return 1;
    if (nargs >= 3) {
	if (keypadnum > 0) {
#ifdef NCURSES_MOUSE_VERSION
	    if (nargs >= 4 && keypadnum == KEY_MOUSE) {
		MEVENT mevent;
		char digits[DIGBUFSIZE];
		LinkList margs;
		const struct zdraw_mouse_event *zcmmp = zdraw_mouse_map;

		if (!setsparam(args[2], ztrdup("MOUSE")))
		    return 1;
		if (getmouse(&mevent) == ERR) {
		    /*
		     * This may happen if the mouse wasn't in
		     * the window, so set the array to empty
		     * but return success unless the set itself
		     * failed.
		     */
		    return !setaparam(args[3], mkarray(NULL));
		}
		margs = newlinklist();
		sprintf(digits, "%d", (int)mevent.id);
		addlinknode(margs, dupstring(digits));
		sprintf(digits, "%d", mevent.x);
		addlinknode(margs, dupstring(digits));
		sprintf(digits, "%d", mevent.y);
		addlinknode(margs, dupstring(digits));
		sprintf(digits, "%d", mevent.z);
		addlinknode(margs, dupstring(digits));

		/*
		 * We only expect one event, but it doesn't hurt
		 * to keep testing.
		 */
		for (; zcmmp->button; zcmmp++) {
		    if (mevent.bstate & zcmmp->event) {
			const struct zdraw_namenumberpair *zcmelp =
			    zdraw_mouse_event_list;
			for (; zcmelp->name; zcmelp++) {
			    if (zcmelp->number == zcmmp->what) {
				char *evstr = zhalloc(strlen(zcmelp->name)+2);
				sprintf(evstr, "%s%d", zcmelp->name,
					zcmmp->button);
				addlinknode(margs, evstr);

				break;
			    }
			}
		    }
		}
		if (mevent.bstate & BUTTON_SHIFT)
		    addlinknode(margs, "SHIFT");
		if (mevent.bstate & BUTTON_CTRL)
		    addlinknode(margs, "CTRL");
		if (mevent.bstate & BUTTON_ALT)
		    addlinknode(margs, "ALT");
		if (!setaparam(args[3], zdraw_list_array(margs)))
		    return 1;
	    } else {
#endif
		const struct zdraw_namenumberpair *nnptr;
		char fbuf[DIGBUFSIZE+1];

		for (nnptr = keypad_names; nnptr->name; nnptr++) {
		    if (keypadnum == nnptr->number) {
			if (!setsparam(args[2], ztrdup(nnptr->name)))
			    return 1;
			return 0;
		    }
		}
		if (keypadnum > KEY_F0) {
		    /* assume it's a function key */
		    sprintf(fbuf, "F%d", keypadnum - KEY_F0);
		} else {
		    /* print raw number */
		    sprintf(fbuf, "%d", keypadnum);
		}
		if (!setsparam(args[2], ztrdup(fbuf)))
		    return 1;
#ifdef NCURSES_MOUSE_VERSION
	    }
#endif
	} else {
	    if (!setsparam(args[2], ztrdup("")))
		return 1;
	}
    }
#ifdef NCURSES_MOUSE_VERSION
    if (keypadnum != KEY_MOUSE && nargs >= 4)
	return !setaparam(args[3], mkarray(NULL));
#endif
    return 0;
}


static int
zccmd_timeout(const char *nam, char **args)
{
    LinkNode node;
    ZCWin w;
    int to;
    char *eptr;

    node = zdraw_validate_window(args[0], ZDRAW_USED);
    if (node == NULL) {
	zwarnnam(nam, "%s: %s", zdraw_strerror(zc_errno), args[0]);
	return 1;
    }

    w = (ZCWin)getdata(node);

    to = (int)zstrtol(args[1], &eptr, 10);
    if (*eptr) {
	zwarnnam(nam, "timeout requires an integer: %s", args[1]);
	return 1;
    }

#if defined(__sun__) && defined(__SVR4) && !defined(HAVE_USE_DEFAULT_COLORS)
    /*
     * On Solaris turning a timeout off seems to be problematic.
     * The following fixes it.  We test for Solaris without ncurses
     * (the last test) to be specific; this may turn up in other older
     * versions of curses, but it's difficult to test for.
     */
    if (to < 0) {
	nocbreak();
	cbreak();
    }
#endif
    wtimeout(w->win, to);
    w->timeout = to;
    return 0;
}


static int
zccmd_mouse(const char *nam, char **args)
{
#ifdef NCURSES_MOUSE_VERSION
    int ret = 0;

    for (; *args; args++) {
	if (!strcmp(*args, "delay")) {
	    int delay;

	    if (!*++args || zdraw_nonnegative(*args, &delay)) {
		zwarnnam(nam, "mouse delay requires a nonnegative integer argument");
		return 1;
	    }
	    mouseinterval(delay);
	} else {
	    char *arg = *args;
	    int onoff = 1;
	    if (*arg == '+')
		arg++;
	    else if (*arg == '-') {
		arg++;
		onoff = 0;
	    }
	    if (!strcmp(arg, "motion")) {
		mmask_t old_mask = zdraw_mouse_mask;
		if (onoff)
		    zdraw_mouse_mask |= REPORT_MOUSE_POSITION;
		else
		    zdraw_mouse_mask &= ~REPORT_MOUSE_POSITION;
		if (old_mask != zdraw_mouse_mask)
		    zdraw_flags |= ZCF_MOUSE_MASK_CHANGED;
	    } else {
		zwarnnam(nam, "unrecognised mouse command: %s", *arg);
		return 1;
	    }
	}
    }

    return ret;
#else
    return 1;
#endif
}


static int
zccmd_position(const char *nam, char **args)
{
    LinkNode node;
    ZCWin w;
    int i, intarr[6];
    char **array, dbuf[DIGBUFSIZE];

    node = zdraw_validate_window(args[0], ZDRAW_USED);
    if (node == NULL) {
	zwarnnam(nam, "%s: %s", zdraw_strerror(zc_errno), args[0]);
	return 1;
    }

    w = (ZCWin)getdata(node);

    /* Look no pointers:  these are macros. */
    getyx(w->win, intarr[0], intarr[1]);
    if (intarr[0] == -1)
	return 1;
    if (w->flags & ZCWF_PAD) {
        intarr[2] = intarr[3] = -1;
    } else {
        getbegyx(w->win, intarr[2], intarr[3]);
        if (intarr[2] == -1)
            return 1;
    }
    getmaxyx(w->win, intarr[4], intarr[5]);
    if (intarr[4] == -1)
	return 1;

    array = (char **)zalloc(7*sizeof(char *));
    for (i = 0; i < 6; i++) {
	sprintf(dbuf, "%d", intarr[i]);
	array[i] = ztrdup(dbuf);
    }
    array[6] = NULL;

    return !setaparam(args[1], array);
}


static int
zccmd_querychar(const char *nam, char **args)
{
    LinkNode node;
    ZCWin w;
    short cp;
    Colorpairnode cpn;
    const struct zdraw_namenumberpair *zattrp;
    LinkList clist;
#if defined(HAVE_WIN_WCH) && defined(HAVE_GETCCHAR)
    attr_t attrs;
    wchar_t *c;
    cchar_t cc;
    int count;
    VARARR(char, instr, 2*MB_CUR_MAX+1);
#else
    chtype inc, attrs;
    char instr[3];
#endif

    node = zdraw_validate_window(args[0], ZDRAW_USED);
    if (node == NULL) {
	zwarnnam(nam, "%s: %s", zdraw_strerror(zc_errno), args[0]);
	return 1;
    }

    w = (ZCWin)getdata(node);

#if defined(HAVE_WIN_WCH) && defined(HAVE_GETCCHAR)
    if (win_wch(w->win, &cc) == ERR)
	return 1;

    /* getcchar writes a terminated string, including any combining marks.
     * Keep the existing first-character result, but provide the full buffer. */
    count = getcchar(&cc, NULL, NULL, NULL, NULL);
    if (count <= 0)
	return 1;
    c = (wchar_t *)zhalloc(count * sizeof(wchar_t));
    if (getcchar(&cc, c, &attrs, &cp, NULL) == ERR)
	return 1;
    /* only overwrite with workaround if we do get 0, the winch method
     * is limited to 256 color pairs */
    if (!cp)
	/* Hmmm... I always get 0 for cp, whereas the following works... */
	cp = PAIR_NUMBER(winch(w->win));

    count = wctomb(instr, c[0]);
    if (count == -1)
	return 1;
    (void)metafy(instr, count, META_NOALLOC);
#else
    inc = winch(w->win);
    /* I think the following is correct, the manual is a little terse */
    cp = PAIR_NUMBER(inc);
    attrs = inc & A_ATTRIBUTES;
    inc &= A_CHARTEXT;
    if (imeta(inc)) {
	instr[0] = Meta;
	instr[1] = (unsigned char) (inc ^ 32);
	instr[2] = '\0';
    } else {
  	instr[0] = (unsigned char) inc;
	instr[1] = '\0';
    }
#endif

    /*
     * Attribute numbers vary, so make a linked list.
     * This also saves us from doing the permanent allocation till
     * the end.
     */
    clist = newlinklist();
    /* First the (possibly multibyte) character itself. */
    addlinknode(clist, instr);
    /*
     * Next the colo[u]r.
     * We should be able to match it in the colorpair list, but
     * if some reason we can't, fail safe and output the number.
     */
    cpn = zdraw_colorget_reverse(cp);
    if (cpn) {
	addlinknode(clist, cpn->node.nam);
    } else {
	/* report color pair number */
	char digits[DIGBUFSIZE];
	sprintf(digits, "%d", (int)cp);
	addlinknode(clist, dupstring(digits));
    }
    /* Now see what attributes are present. */
    for (zattrp = zdraw_attributes; zattrp->name; zattrp++) {
	if (attrs & zattrp->number)
	    addlinknode(clist, zattrp->name);
    }

    /* Turn this into an array and store it. */
    return !setaparam(args[1] ? args[1] : "reply", zdraw_list_array(clist));
}


static int
zccmd_touch(const char *nam, char **args)
{
    LinkNode node;
    ZCWin w;
    int ret = 0;

    for (; *args; args++) {
	node = zdraw_validate_window(args[0], ZDRAW_USED);
	if (node == NULL) {
	    zwarnnam(nam, "%s: %s", zdraw_strerror(zc_errno), args[0]);
	    return 1;
	}

	w = (ZCWin)getdata(node);
	if (touchwin(w->win) != OK)
	    ret = 1;
    }

    return ret;
}

/* Query the terminal, not curses' cached window dimensions. */
static int
zdraw_terminal_size(int *rows, int *cols)
{
#ifdef TIOCGWINSZ
    struct winsize size;
    int fd = SHTTY, closefd = 0, ret;
    if (fd == -1) {
        fd = open("/dev/tty", O_RDWR | O_NOCTTY);
        if (fd == -1)
            return 1;
        closefd = 1;
    }
    ret = ioctl(fd, TIOCGWINSZ, (char *)&size);
    if (closefd)
        close(fd);
    if (ret == -1 || !size.ws_row || !size.ws_col)
        return 1;
    *rows = size.ws_row;
    *cols = size.ws_col;
    return 0;
#else
    (void)rows;
    (void)cols;
    return 2;
#endif
}

static int
zccmd_geometry(UNUSED(const char *nam), char **args)
{
    int rows, cols, result = zdraw_terminal_size(&rows, &cols);
    char **array, digits[32];
    if (result)
        return result;
    array = (char **)zalloc(3 * sizeof(char *));
    sprintf(digits, "%d", rows);
    array[0] = ztrdup(digits);
    sprintf(digits, "%d", cols);
    array[1] = ztrdup(digits);
    array[2] = NULL;
    return !setaparam(args[0], array) || (errflag & ERRFLAG_ERROR);
}

/* A negative value denotes unavailable information, not a negative count. */
static void
zdraw_colorinfo_value(LinkList list, const char *key, zlong value)
{
    char digits[DIGBUFSIZE];

    addlinknode(list, dupstring(key));
    if (value < 0)
	addlinknode(list, dupstring("unknown"));
    else {
	convbase(digits, value, 10);
	addlinknode(list, dupstring(digits));
    }
}

static int
zdraw_association(const char *nam, char *name)
{
    Param pm;
    if (!isident(name) || strchr(name, '[')) {
        zwarnnam(nam, "expected an associative parameter name");
        return 1;
    }
    pm = (Param)gethashnode2(paramtab, name);
    if (pm && ((pm->node.flags & (PM_READONLY|PM_SPECIAL)) ||
               PM_TYPE(pm->node.flags) != PM_HASHED)) {
        zwarnnam(nam, "expected an ordinary writable associative parameter: %s", name);
        return 1;
    }
    return 0;
}

static int
zccmd_rowinfo(const char *nam, char **args)
{
#if defined(ZDRAW_WIDE_SPANS) || defined(HAVE_WADDCHNSTR)
    struct zdraw_prepared *p;
    LinkList info;
    if (zdraw_association(nam, args[1]))
        return 1;
    p = zdraw_prepared_rows ?
        (struct zdraw_prepared *)gethashnode2(zdraw_prepared_rows, args[0]) : NULL;
    if (!p) {
        zwarnnam(nam, "unknown prepared row: %s", args[0]);
        return 1;
    }
    info = newlinklist();
    zdraw_colorinfo_value(info, "width", p->row.width);
    zdraw_colorinfo_value(info, "cells", p->row.count);
    zdraw_colorinfo_value(info, "draws", p->draws);
    zdraw_colorinfo_value(info, "bytes", (zlong)p->bytes);
    zdraw_colorinfo_value(info, "session_bytes", (zlong)zdraw_prepared_bytes);
    zdraw_colorinfo_value(info, "session_limit", (zlong)ZDRAW_PREPARED_LIMIT);
    zdraw_colorinfo_value(info, "multibyte", p->multibyte);
    addlinknode(info, "locale");
    addlinknode(info, p->locale);
    return !sethparam(args[1], zdraw_list_array(info)) || (errflag & ERRFLAG_ERROR);
#else
    (void)nam;
    (void)args;
    return 2;
#endif
}

/* Read the current cell without moving the cursor, touching the window or
 * changing conversion state used by the shell's multibyte input helpers. */
static int
zdraw_cell_record(const char *nam, WINDOW *win, LinkList *record)
{
    LinkList info, names;
    Colorpairnode color;
    const struct zdraw_namenumberpair *entry;
    char *text, digits[DIGBUFSIZE];
    short pair;
    int row, column, characters;
#if defined(HAVE_WIN_WCH) && defined(HAVE_GETCCHAR)
    cchar_t cell;
    attr_t attrs;
    wchar_t *wide;
    int count;
    size_t bytes;
#else
    chtype cell, attrs;
    char raw;
#endif

    getyx(win, row, column);
#if defined(HAVE_WIN_WCH) && defined(HAVE_GETCCHAR)
    if (win_wch(win, &cell) == ERR)
        return 1;
    count = getcchar(&cell, NULL, NULL, NULL, NULL);
    if (count <= 0 || (size_t)count > (size_t)-1 / sizeof(*wide))
        return 1;
    wide = (wchar_t *)zhalloc((size_t)count * sizeof(*wide));
    if (getcchar(&cell, wide, &attrs, &pair, NULL) == ERR)
        return 1;
    bytes = wcstombs(NULL, wide, 0);
    if (bytes == (size_t)-1 || bytes > (INT_MAX - 1) / 2) {
        zwarnnam(nam, "cell text cannot be represented in the current locale");
        return 1;
    }
    text = zhalloc(2 * bytes + 1);
    if (wcstombs(text, wide, bytes + 1) != bytes)
        return 1;
    (void)metafy(text, (int)bytes, META_NOALLOC);
    characters = count - 1;
#else
    cell = winch(win);
    if (cell == (chtype)ERR)
        return 1;
    attrs = cell & A_ATTRIBUTES;
    pair = PAIR_NUMBER(cell);
    raw = (char)(cell & A_CHARTEXT);
    text = metafy(&raw, 1, META_HEAPDUP);
    characters = 1;
#endif
    names = newlinklist();
    for (entry = zdraw_attributes; entry->name; entry++) {
        if (attrs & entry->number)
            addlinknode(names, entry->name);
    }
#ifdef A_ALTCHARSET
    if (attrs & A_ALTCHARSET)
        addlinknode(names, "altcharset");
#endif
    info = newlinklist();
    addlinknode(info, "text");
    addlinknode(info, text);
    addlinknode(info, "attributes");
    addlinknode(info, zjoin(hlinklist2array(names, 0), ' ', 1));
    /* Include all non-color attribute bits for diagnostics, even flags without
     * a portable name in this module. The number is library-specific. */
    sprintf(digits, "%lu", (unsigned long)(attrs & ~A_COLOR));
    addlinknode(info, "attribute_bits");
    addlinknode(info, dupstring(digits));
    color = zdraw_colorget_reverse(pair);
    addlinknode(info, "color");
    addlinknode(info, color ? color->node.nam : "unknown");
    addlinknode(info, "color_source");
    addlinknode(info, color ? "cache" : "unknown");
    addlinknode(info, "encoding");
#if defined(HAVE_WIN_WCH) && defined(HAVE_GETCCHAR)
    addlinknode(info, "multibyte");
#else
    addlinknode(info, "byte");
#endif
    zdraw_colorinfo_value(info, "pair", pair);
    zdraw_colorinfo_value(info, "characters", characters);
    zdraw_colorinfo_value(info, "row", row);
    zdraw_colorinfo_value(info, "column", column);
    *record = info;
    return 0;
}

static int
zccmd_cellinfo(const char *nam, char **args)
{
    LinkNode node;
    LinkList info;
    if (zdraw_association(nam, args[1]))
        return 1;
    node = zdraw_validate_window(args[0], ZDRAW_USED);
    if (!node) {
        zwarnnam(nam, "%s: %s", zdraw_strerror(zc_errno), args[0]);
        return 1;
    }
    if (zdraw_cell_record(nam, ((ZCWin)getdata(node))->win, &info))
        return 1;
    return !sethparam(args[1], zdraw_list_array(info)) || (errflag & ERRFLAG_ERROR);
}

#define ZDRAW_SNAPSHOT_CELLS 65536
#define ZDRAW_SNAPSHOT_BYTES ((size_t)16 * 1024 * 1024)

static int
zdraw_snapshot_pair(LinkList info, char *key, char *value, size_t *bytes)
{
    size_t k = strlen(key) + 1, v = strlen(value) + 1;
    if (k > ZDRAW_SNAPSHOT_BYTES - *bytes ||
        v > ZDRAW_SNAPSHOT_BYTES - *bytes - k)
        return 1;
    *bytes += k + v;
    addlinknode(info, dupstring(key));
    addlinknode(info, value);
    return 0;
}

/* Public readback does not expose portable wide-cell continuation flags.
 * Infer only complete equal-cell runs bounded by unlike cells on BOTH sides.
 * Clipped/shared edges and malformed runs remain explicitly unknown. */
static char *
zdraw_record_value(LinkList record, const char *key)
{
    LinkNode node;
    for (node = firstnode(record); node;) {
        char *name = (char *)getdata(node);
        incnode(node);
        if (!strcmp(name, key)) return (char *)getdata(node);
        incnode(node);
    }
    return "";
}

static int
zdraw_record_width(LinkList record)
{
    char *text = zdraw_record_value(record, "text");
    int width, result, wide = 0;
    convchar_t wc;
    if (!strcmp(zdraw_record_value(record, "encoding"), "multibyte")) wide = 1;
    MB_METACHARINIT();
    result = zdraw_text_next(&text, wide, &wc, &width);
    return result ? 0 : width;
}

static int
zdraw_same_cell(LinkList a, LinkList b)
{
    return !strcmp(zdraw_record_value(a, "text"), zdraw_record_value(b, "text")) &&
        !strcmp(zdraw_record_value(a, "attribute_bits"), zdraw_record_value(b, "attribute_bits")) &&
        !strcmp(zdraw_record_value(a, "pair"), zdraw_record_value(b, "pair"));
}

static void
zdraw_occupancy(LinkList *cells, int cols)
{
    int x = 0, end, width, i, base;
    unsigned long supported = 0;
    const struct zdraw_namenumberpair *entry;
    for (entry = zdraw_attributes; entry->name; entry++)
        supported |= entry->number;
    while (x < cols) {
        width = zdraw_record_width(cells[x]);
        end = x + 1;
        if (width > 1)
            while (end < cols && zdraw_same_cell(cells[x], cells[end])) end++;
        for (i = x; i < end; i++) {
            const char *kind = "unknown", *source = "unknown";
            base = -1;
            if (width == 1) {
                kind = "single"; source = "readback"; base = i;
            } else if (width > 1 && x > 0 && end < cols && (end-x) % width == 0) {
                base = x + ((i-x) / width) * width;
                kind = i == base ? "base" : "continuation";
                source = "inferred";
            }
            addlinknode(cells[i], "style_supported");
            addlinknode(cells[i], (strtoul(zdraw_record_value(cells[i], "attribute_bits"),
                NULL, 10) & ~supported) ? "no" : "yes");
            addlinknode(cells[i], "occupancy"); addlinknode(cells[i], (void *)kind);
            addlinknode(cells[i], "occupancy_source"); addlinknode(cells[i], (void *)source);
            zdraw_colorinfo_value(cells[i], "base_column", base);
            zdraw_colorinfo_value(cells[i], "cell_width", width ? width : -1);
        }
        x = end;
    }
}

static int
zccmd_snapshot(const char *nam, char **args)
{
    LinkNode node, field;
    LinkList info, cell;
    WINDOW *win, *copy;
    LinkList *line = NULL;
    int occupancy = args[2] != NULL;
    int rows, cols, y, x, cursor_y, cursor_x, result = 0;
    size_t bytes = 0;
    char key[3 * DIGBUFSIZE + 32];

    if (occupancy && strcmp(args[2], "occupancy")) return 1;
    if (zdraw_association(nam, args[1]))
        return 1;
    node = zdraw_validate_window(args[0], ZDRAW_USED);
    if (!node) {
        zwarnnam(nam, "%s: %s", zdraw_strerror(zc_errno), args[0]);
        return 1;
    }
    win = ((ZCWin)getdata(node))->win;
    getmaxyx(win, rows, cols);
    getyx(win, cursor_y, cursor_x);
    if (rows <= 0 || cols <= 0 || rows > ZDRAW_SNAPSHOT_CELLS / cols) {
        zwarnnam(nam, "snapshot exceeds the cell limit");
        return 1;
    }
    /* Even moving and restoring the live cursor can affect automatic input
     * refresh. Inspect an independent copy, including for subwindows. */
    copy = dupwin(win);
    if (!copy) {
        zwarnnam(nam, "failed to copy window for snapshot");
        return 1;
    }
    info = newlinklist();
    addlinknode(info, "format");
    addlinknode(info, "zdraw-snapshot-1");
    addlinknode(info, "layout");
    addlinknode(info, "readback");
    zdraw_colorinfo_value(info, "rows", rows);
    zdraw_colorinfo_value(info, "columns", cols);
    zdraw_colorinfo_value(info, "cursor_row", cursor_y);
    zdraw_colorinfo_value(info, "cursor_column", cursor_x);
    zdraw_colorinfo_value(info, "cell_count", (zlong)rows * cols);
    zdraw_colorinfo_value(info, "cell_limit", ZDRAW_SNAPSHOT_CELLS);
    zdraw_colorinfo_value(info, "byte_limit", (zlong)ZDRAW_SNAPSHOT_BYTES);
    for (field = firstnode(info); field; incnode(field)) {
        bytes += strlen((char *)getdata(field)) + 1;
    }
    if (bytes > ZDRAW_SNAPSHOT_BYTES)
        result = 1;
    if (occupancy) line = (LinkList *)zhalloc((size_t)cols * sizeof(*line));
    for (y = 0; y < rows && !result; y++) {
        if (occupancy) {
            for (x = 0; x < cols; x++) {
                if (wmove(copy, y, x) == ERR || zdraw_cell_record(nam, copy, &line[x])) {
                    result = 1; break;
                }
            }
            if (result) break;
            zdraw_occupancy(line, cols);
        }
        for (x = 0; x < cols && !result; x++) {
            if (occupancy) cell = line[x];
            else if (wmove(copy, y, x) == ERR || zdraw_cell_record(nam, copy, &cell)) {
                result = 1;
                break;
            }
            for (field = firstnode(cell); field;) {
                char *name = (char *)getdata(field), *value;
                incnode(field);
                value = (char *)getdata(field);
                incnode(field);
                sprintf(key, "%d,%d,%s", y, x, name);
                if (zdraw_snapshot_pair(info, key, value, &bytes)) {
                    zwarnnam(nam, "snapshot exceeds the key/value byte limit");
                    result = 1;
                    break;
                }
            }
        }
    }
    if (delwin(copy) == ERR)
        result = 1;
    if (result)
        return 1;
    return !sethparam(args[1], zdraw_list_array(info)) || (errflag & ERRFLAG_ERROR);
}

#include "zdraw_raster.h"

/* Logical counters are deliberately independent of curses allocator details.
 * Saturate totals rather than overflowing on an unbounded inherited window list. */
static void
zdraw_resource_add(zlong *total, zlong amount)
{
    *total = amount > ZLONG_MAX - *total ? ZLONG_MAX : *total + amount;
}

static int
zccmd_resourceinfo(const char *nam, char **args)
{
    LinkList info;
    LinkNode node;
    zlong windows = 0, children = 0, owned = 0, cells = 0, backing = 0;
    zlong pads = 0, retired = 0, input_pads = 0;
    int rows, cols;
#ifdef ZDRAW_WINDOW_TREE
    int i;
#endif
    if (zdraw_association(nam, args[0]))
        return 1;
    /* No cleanup, allocation of surfaces, input, touching or refresh here. */
    if (zdraw_windows) {
        for (node = firstnode(zdraw_windows); node; incnode(node)) {
            ZCWin w = (ZCWin)getdata(node);
            if (w->flags & ZCWF_PAD) {
                zdraw_resource_add(&pads, 1);
                continue;
            }
            getmaxyx(w->win, rows, cols);
            zdraw_resource_add(&windows, 1);
            zdraw_resource_add(&cells, (zlong)rows * cols);
            if (w->parent)
                zdraw_resource_add(&children, 1);
            else {
                zdraw_resource_add(&backing, (zlong)rows * cols);
                if (!(w->flags & ZCWF_PERMANENT))
                    zdraw_resource_add(&owned, 1);
            }
        }
    }
#ifdef ZDRAW_WINDOW_TREE
    for (i = 0; i < zdraw_tree_retired_count; i++)
        if (zdraw_tree_retired[i]) retired++;
#endif
#ifdef NCURSES_VERSION
    input_pads = zdraw_input_pad != NULL;
#endif
    info = newlinklist();
    addlinknode(info, "format"); addlinknode(info, "zdraw-resources-1");
    addlinknode(info, "session");
    addlinknode(info, zdraw_getwindowbyname("stdscr") ?
                (zdraw_suspended ? "suspended" : "active") : "inactive");
    zdraw_colorinfo_value(info, "windows", windows);
    zdraw_colorinfo_value(info, "child_windows", children);
    zdraw_colorinfo_value(info, "owned_windows", owned);
    zdraw_colorinfo_value(info, "window_cells", cells);
    zdraw_colorinfo_value(info, "backing_cells", backing);
    zdraw_colorinfo_value(info, "pads", pads);
    zdraw_colorinfo_value(info, "pad_cells", (zlong)zdraw_pad_cells);
    zdraw_colorinfo_value(info, "private_input_pads", input_pads);
    zdraw_colorinfo_value(info, "retired_tree_windows", retired);
    zdraw_colorinfo_value(info, "cached_color_pairs", zdraw_colorpairs ? zdraw_colorpairs->ct : 0);
    zdraw_colorinfo_value(info, "counter_limit", ZLONG_MAX);
    zdraw_colorinfo_value(info, "raster_surfaces", zdraw_raster_count);
    zdraw_colorinfo_value(info, "raster_bytes", (zlong)zdraw_raster_bytes);
    zdraw_colorinfo_value(info, "raster_byte_limit", (zlong)ZDRAW_RASTER_BYTES);
#if defined(ZDRAW_WIDE_SPANS) || defined(HAVE_WADDCHNSTR)
    zdraw_colorinfo_value(info, "prepared_rows", zdraw_prepared_rows ? zdraw_prepared_rows->ct : 0);
    zdraw_colorinfo_value(info, "prepared_bytes", (zlong)zdraw_prepared_bytes);
    zdraw_colorinfo_value(info, "prepared_created", zdraw_prepared_created);
    zdraw_colorinfo_value(info, "prepared_draws", zdraw_prepared_draws);
    zdraw_colorinfo_value(info, "prepared_byte_limit", (zlong)ZDRAW_PREPARED_LIMIT);
#else
    zdraw_colorinfo_value(info, "prepared_rows", 0);
    zdraw_colorinfo_value(info, "prepared_bytes", 0);
    zdraw_colorinfo_value(info, "prepared_created", 0);
    zdraw_colorinfo_value(info, "prepared_draws", 0);
    zdraw_colorinfo_value(info, "prepared_byte_limit", -1);
#endif
    zdraw_colorinfo_value(info, "pad_cell_limit", ZDRAW_PAD_CELLS);
    zdraw_colorinfo_value(info, "pad_total_cell_limit", ZDRAW_PAD_TOTAL_CELLS);
    zdraw_colorinfo_value(info, "pad_dimension_limit", ZDRAW_PAD_DIMENSION);
    zdraw_colorinfo_value(info, "resize_cell_limit", ZDRAW_RESIZE_CELLS);
    zdraw_colorinfo_value(info, "resize_dimension_limit", ZDRAW_RESIZE_DIMENSION);
    zdraw_colorinfo_value(info, "tree_window_limit", ZDRAW_TREE_WINDOWS);
    zdraw_colorinfo_value(info, "copy_cell_limit", ZDRAW_COPY_CELLS);
    zdraw_colorinfo_value(info, "snapshot_cell_limit", ZDRAW_SNAPSHOT_CELLS);
    zdraw_colorinfo_value(info, "snapshot_byte_limit", (zlong)ZDRAW_SNAPSHOT_BYTES);
    return !sethparam(args[0], zdraw_list_array(info)) || (errflag & ERRFLAG_ERROR);
}

/* Signed event values (mouse coordinates can be outside a window). */
static void
zdraw_event_number(LinkList info, const char *key, zlong value)
{
    char digits[DIGBUFSIZE];
    convbase(digits, value, 10);
    addlinknode(info, dupstring(key));
    addlinknode(info, dupstring(digits));
}


#ifdef ZDRAW_QUERIES
static double
zdraw_query_now(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now))
        return -1;
    return (double)now.tv_sec * 1000 + (double)now.tv_nsec / 1000000;
}
#endif

#ifdef ZDRAW_ENHANCED
static int
zdraw_enhanced_write(const char *sequence)
{
    return fputs(sequence, stdout) == EOF || fflush(stdout) == EOF;
}

static int
zccmd_focus(const char *nam, char **args)
{
    int i, j, code, force = args[1] && !strcmp(args[1], "force");
    char *bound;
    const char *sequences[] = {"\033[I", "\033[O"};
    if (!strcmp(args[0], "off") && !args[1]) {
        if (zdraw_focus_applied && zdraw_enhanced_write("\033[?1004l"))
            return 1;
        zdraw_focus_applied = zdraw_focus_on = 0;
        for (i = 0; i < 2; i++) {
            if (zdraw_focus_owned[i] && define_key(NULL, zdraw_focus_keys[i]) == ERR)
                return 1;
            zdraw_focus_keys[i] = zdraw_focus_owned[i] = 0;
        }
        return 0;
    }
    if (strcmp(args[0], "on") || (args[1] && !force))
        return 1;
    if (zdraw_focus_on)
        return zdraw_enhanced_resume();
    /* A reset report permits owning this mode without disabling another owner. */
    if (!force && zdraw_query_reports[1] != 2) {
        zwarnnam(nam, "focus needs an observed reset mode or explicit force");
        return 2;
    }
    if (!isatty(0) || !isatty(1))
        return 1;
    for (i = 0; i < 2; i++) {
        code = key_defined(sequences[i]);
        if (code) {
#ifdef HAVE_TIGETSTR
            char *declared = tigetstr(i ? "kxOUT" : "kxIN");
            if (code < 0 || !declared || declared == (char *)-1 || strcmp(declared, sequences[i]))
                return 1;
#else
            return 1;
#endif
        }
    }
    for (i = 0; i < 2; i++) {
        code = key_defined(sequences[i]);
        if (code > 0) {
            zdraw_focus_keys[i] = code;
            continue;
        }
        for (code = KEY_MAX + 1; code < KEY_MAX + 257; code++) {
            bound = keybound(code, 0);
            if (!bound) break;
            free(bound);
        }
        if (code == KEY_MAX + 257 || define_key(sequences[i], code) == ERR) {
            for (j = 0; j < i; j++) {
                if (zdraw_focus_owned[j]) define_key(NULL, zdraw_focus_keys[j]);
                zdraw_focus_keys[j] = zdraw_focus_owned[j] = 0;
            }
            return 1;
        }
        zdraw_focus_keys[i] = code;
        zdraw_focus_owned[i] = 1;
    }
    if (zdraw_enhanced_write("\033[?1004h")) {
        zdraw_enhanced_write("\033[?1004l");
        for (i = 0; i < 2; i++) {
            if (zdraw_focus_owned[i]) define_key(NULL, zdraw_focus_keys[i]);
            zdraw_focus_keys[i] = zdraw_focus_owned[i] = 0;
        }
        return 1;
    }
    zdraw_focus_on = zdraw_focus_applied = 1;
    return 0;
}

static int
zccmd_keyboard(const char *nam, char **args)
{
    if (!strcmp(args[0], "off")) {
        if (zdraw_key_used || zdraw_key_discard) {
            zwarnnam(nam, "finish the partial keyboard event before disabling it");
            return 1;
        }
        zdraw_keyboard_on = 0;
        if (zdraw_keyboard_applied) {
            /* Never retry a pop: even a failed flush may have reached the peer. */
            zdraw_keyboard_applied = 0;
            return zdraw_enhanced_write("\033[<u");
        }
        return 0;
    }
    if (strcmp(args[0], "on"))
        return 1;
    if (zdraw_keyboard_on)
        return zdraw_enhanced_resume();
    if (zdraw_query_reports[3] < 0) {
        zwarnnam(nam, "keyboard needs an accepted keyboard_events query reply");
        return 2;
    }
    if (zdraw_paste_active || !isatty(0) || !isatty(1))
        return 1;
    if (zdraw_enhanced_write("\033[>27u")) {
        zdraw_enhanced_write("\033[<u");
        return 1;
    }
    zdraw_keyboard_on = zdraw_keyboard_applied = 1;
    return 0;
}
#else
static int
zccmd_focus(UNUSED(const char *nam), UNUSED(char **args))
{
    return 2;
}
static int
zccmd_keyboard(UNUSED(const char *nam), UNUSED(char **args))
{
    return 2;
}
#endif

static int
zdraw_enhanced_pause(void)
{
#ifdef ZDRAW_ENHANCED
    if (zdraw_keyboard_applied) {
        zdraw_keyboard_applied = 0;
        if (zdraw_enhanced_write("\033[<u"))
            return 1;
    }
    if (zdraw_focus_applied) {
        if (zdraw_enhanced_write("\033[?1004l")) {
            zdraw_enhanced_resume();
            return 1;
        }
        zdraw_focus_applied = 0;
    }
#endif
    return 0;
}

static int
zdraw_enhanced_resume(void)
{
#ifdef ZDRAW_ENHANCED
    if (zdraw_focus_on && !zdraw_focus_applied) {
        if (zdraw_enhanced_write("\033[?1004h"))
            return 1;
        zdraw_focus_applied = 1;
    }
    if (zdraw_keyboard_on && !zdraw_keyboard_applied) {
        if (zdraw_enhanced_write("\033[>27u")) {
            zdraw_enhanced_write("\033[<u");
            return 1;
        }
        zdraw_keyboard_applied = 1;
    }
#endif
    return 0;
}

static void
zdraw_enhanced_cleanup(void)
{
#ifdef ZDRAW_ENHANCED
    int i;
    /* Cleanup must never roll back into re-enabling another protocol. */
    if (zdraw_keyboard_applied) {
        zdraw_keyboard_applied = 0;
        zdraw_enhanced_write("\033[<u");
    }
    if (zdraw_focus_applied)
        zdraw_enhanced_write("\033[?1004l");
    for (i = 0; i < 2; i++) {
        if (zdraw_focus_owned[i]) define_key(NULL, zdraw_focus_keys[i]);
        zdraw_focus_keys[i] = zdraw_focus_owned[i] = 0;
    }
    zdraw_focus_on = zdraw_focus_applied = 0;
    zdraw_keyboard_on = zdraw_keyboard_applied = 0;
    zdraw_key_used = zdraw_key_discard = 0;
#endif
}

#ifdef ZDRAW_ENHANCED
static int
zdraw_focus_event(char *target, int focused)
{
    LinkList info = newlinklist();
    addlinknode(info, "type"); addlinknode(info, "focus");
    addlinknode(info, "source"); addlinknode(info, "focus-report");
    addlinknode(info, "focused"); addlinknode(info, focused ? "1" : "0");
    addlinknode(info, "text"); addlinknode(info, "");
    return !sethparam(target, zdraw_list_array(info)) || (errflag & ERRFLAG_ERROR);
}

static int
zdraw_keyboard_unknown(char *target, const char *reason)
{
    LinkList info = newlinklist();
    addlinknode(info, "type"); addlinknode(info, "unknown");
    addlinknode(info, "source"); addlinknode(info, "kitty");
    addlinknode(info, "supported"); addlinknode(info, "no");
    addlinknode(info, "reason"); addlinknode(info, dupstring(reason));
    addlinknode(info, "key"); addlinknode(info, "UNKNOWN");
    addlinknode(info, "code"); addlinknode(info, "unknown");
    addlinknode(info, "action"); addlinknode(info, "unknown");
    addlinknode(info, "modifiers"); addlinknode(info, "unknown");
    addlinknode(info, "encoding"); addlinknode(info, "byte");
    addlinknode(info, "text_status"); addlinknode(info, "none");
    addlinknode(info, "text"); addlinknode(info, "");
    addlinknode(info, "raw"); addlinknode(info, metafy(zdraw_key_buffer, zdraw_key_used, META_HEAPDUP));
    zdraw_key_used = 0;
    return !sethparam(target, zdraw_list_array(info)) || (errflag & ERRFLAG_ERROR);
}

static int
zdraw_keyboard_scalar(unsigned value)
{
    return value <= 0x10ffff && !(value >= 0xd800 && value <= 0xdfff);
}

/* Text is transmitted as scalar values, not inferred from key identity. */
static int
zdraw_keyboard_utf8(unsigned value, char *out)
{
    if (value < 0x80) { out[0] = value; return 1; }
    if (value < 0x800) {
        out[0] = 0xc0 | (value >> 6); out[1] = 0x80 | (value & 63); return 2;
    }
    if (value < 0x10000) {
        out[0] = 0xe0 | (value >> 12); out[1] = 0x80 | ((value >> 6) & 63);
        out[2] = 0x80 | (value & 63); return 3;
    }
    out[0] = 0xf0 | (value >> 18); out[1] = 0x80 | ((value >> 12) & 63);
    out[2] = 0x80 | ((value >> 6) & 63); out[3] = 0x80 | (value & 63); return 4;
}

static int
zdraw_keyboard_record(char *target)
{
    unsigned values[3][16] = {{0}}, code, modifiers, action;
    int present[3][16] = {{0}};
    int counts[3] = {1, 1, 1}, field = 0, sub = 0, i, digits = 0, textlen = 0;
    char final = zdraw_key_buffer[zdraw_key_used - 1], text[64], key[32];
    char *mods[9];
    int nmods = 0;
    const char *modifier_names[] = {"SHIFT", "ALT", "CTRL", "SUPER", "HYPER", "META", "CAPS_LOCK", "NUM_LOCK"};
    const char *name = NULL;
    LinkList info;
    if (zdraw_key_used < 3 || zdraw_key_buffer[0] != '\033' || zdraw_key_buffer[1] != '[')
        return zdraw_keyboard_unknown(target, "unsupported-sequence");
    values[1][0] = 1;
    values[1][1] = 1;
    for (i = 2; i < zdraw_key_used - 1; i++) {
        unsigned ch = (unsigned char)zdraw_key_buffer[i];
        if (ch >= '0' && ch <= '9') {
            present[field][sub] = 1;
            if (!digits) values[field][sub] = 0;
            if (++digits > 7 || values[field][sub] > (0x10ffff - (ch - '0')) / 10)
                return zdraw_keyboard_unknown(target, "numeric-limit");
            values[field][sub] = values[field][sub] * 10 + ch - '0';
        } else if (ch == ':' || ch == ';') {
            if (ch == ';') {
                if (++field > 2) return zdraw_keyboard_unknown(target, "field-limit");
                sub = 0;
            } else {
                sub++;
                if (sub >= (field == 0 ? 3 : field == 1 ? 2 : 16))
                    return zdraw_keyboard_unknown(target, "field-limit");
                counts[field]++;
            }
            digits = 0;
        } else return zdraw_keyboard_unknown(target, "unsupported-sequence");
    }
    code = values[0][0]; modifiers = values[1][0]; action = values[1][1];
    if (!modifiers || modifiers > 256 || action < 1 || action > 3)
        return zdraw_keyboard_unknown(target, "unsupported-modifiers-or-action");
    modifiers--;
    if (final == 'u') {
        if (!present[0][0]) return zdraw_keyboard_unknown(target, "missing-key");
        if (!zdraw_keyboard_scalar(code)) return zdraw_keyboard_unknown(target, "invalid-scalar");
        for (i = 1; i < counts[0]; i++)
            if (!zdraw_keyboard_scalar(values[0][i])) return zdraw_keyboard_unknown(target, "invalid-scalar");
        switch (code) {
            case 27: name = "ESC"; break; case 13: name = "ENTER"; break;
            case 9: name = "TAB"; break; case 127: name = "BACKSPACE"; break;
        }
        if (!name && code >= 57376 && code <= 57398) {
            sprintf(key, "F%u", code - 57363); name = key;
        }
        if (!name) {
            if (code >= 57344 && code <= 63743) name = "UNKNOWN";
            else { sprintf(key, "U+%04X", code); name = key; }
        }
    } else {
        /* Kitty retains these CSI functional encodings, adding event subfields. */
        if (counts[0] != 1 || field > 1) return zdraw_keyboard_unknown(target, "unsupported-functional");
        if (final == '~') {
            static const struct zdraw_namenumberpair functions[] = {
                {"IC",2},{"DC",3},{"PPAGE",5},{"NPAGE",6},{"HOME",7},{"END",8},
                {"F1",11},{"F2",12},{"F3",13},{"F4",14},{"F5",15},{"F6",17},
                {"F7",18},{"F8",19},{"F9",20},{"F10",21},{"F11",23},{"F12",24},
                {"MENU",29},{NULL,0}
            };
            for (i = 0; functions[i].name; i++)
                if ((unsigned)functions[i].number == code) { name = functions[i].name; break; }
        } else if (code == 0 || code == 1) {
            switch (final) {
                case 'A': name="UP"; break; case 'B': name="DOWN"; break;
                case 'C': name="RIGHT"; break; case 'D': name="LEFT"; break;
                case 'H': name="HOME"; break; case 'F': name="END"; break;
                case 'P': name="F1"; break; case 'Q': name="F2"; break; case 'S': name="F4"; break;
            }
        }
        if (!name) return zdraw_keyboard_unknown(target, "unsupported-functional");
    }
    if (field == 2 && (present[2][0] || counts[2] > 1)) {
        for (i = 0; i < counts[2]; i++) {
            if (!present[2][i] || !zdraw_keyboard_scalar(values[2][i])) return zdraw_keyboard_unknown(target, "invalid-text-scalar");
            textlen += zdraw_keyboard_utf8(values[2][i], text + textlen);
        }
    }
    info = newlinklist();
    addlinknode(info, "type"); addlinknode(info, "key");
    addlinknode(info, "source"); addlinknode(info, "kitty");
    addlinknode(info, "key"); addlinknode(info, dupstring(name));
    addlinknode(info, "supported"); addlinknode(info, strcmp(name, "UNKNOWN") ? "yes" : "no");
    zdraw_event_number(info, "code", code);
    addlinknode(info, "code_kind"); addlinknode(info, final == 'u' ? "unicode" : "functional");
    addlinknode(info, "action"); addlinknode(info, action == 1 ? "press" : action == 2 ? "repeat" : "release");
    for (i = 0; i < 8; i++) if (modifiers & (1u << i)) mods[nmods++] = (char *)modifier_names[i];
    mods[nmods] = NULL;
    addlinknode(info, "modifiers"); addlinknode(info, zjoin(mods, ' ', 1));
    zdraw_event_number(info, "modifier_bits", modifiers);
    addlinknode(info, "text"); addlinknode(info, metafy(text, textlen, META_HEAPDUP));
    addlinknode(info, "encoding"); addlinknode(info, "utf-8");
    addlinknode(info, "text_status"); addlinknode(info, field == 2 && present[2][0] ? "provided" : "none");
    addlinknode(info, "shifted_key"); addlinknode(info, "unknown");
    addlinknode(info, "base_key"); addlinknode(info, "unknown");
    /* Alternate identities are parsed and validated, but not requested or exposed. */
    addlinknode(info, "raw"); addlinknode(info, metafy(zdraw_key_buffer, zdraw_key_used, META_HEAPDUP));
    zdraw_key_used = 0;
    return !sethparam(target, zdraw_list_array(info)) || (errflag & ERRFLAG_ERROR);
}

/* Curses first recognizes its known sequences. Only an otherwise literal ESC
 * enters this bounded CSI tail reader, on that same curses input queue. */
static int
zdraw_keyboard_read(WINDOW *win, int timeout, char *target, int initial)
{
    int ch, reads = 0, result;
    double now = zdraw_query_now(), remaining;
    if (initial) {
        wtimeout(win, 0); keypad(win, FALSE);
        ch = wgetch(win);
        keypad(win, TRUE); wtimeout(win, timeout);
        if (ch != '[') {
            if (ch != ERR && ungetch(ch) == ERR) {
                zdraw_key_buffer[0] = '\033'; zdraw_key_buffer[1] = ch;
                zdraw_key_used = 2;
                return zdraw_keyboard_unknown(target, "queue-failure");
            }
            return -1; /* Standalone ESC or legacy non-CSI input. */
        }
        zdraw_key_buffer[0] = '\033'; zdraw_key_buffer[1] = '[';
        zdraw_key_used = 2; zdraw_key_deadline = now + 250;
    }
    if (now < 0 || now >= zdraw_key_deadline) {
        zdraw_key_discard = 0;
        return zdraw_keyboard_unknown(target, "timeout");
    }
    remaining = zdraw_key_deadline - now;
    if (initial) wtimeout(win, 0);
    else if (timeout < 0 || timeout > remaining) wtimeout(win, (int)remaining + 1);
    keypad(win, FALSE);
    while (reads++ < ZDRAW_KEY_BYTES) {
        ch = wgetch(win);
        wtimeout(win, 0);
        if (ch == ERR || ch > 255) break;
        if (ch == '\033') {
            ungetch(ch);
            keypad(win, TRUE); wtimeout(win, timeout);
            zdraw_key_discard = 0;
            return zdraw_keyboard_unknown(target, "interrupted-sequence");
        }
        zdraw_key_buffer[zdraw_key_used++] = ch;
        if (ch >= 0x40 && ch <= 0x7e) {
            keypad(win, TRUE); wtimeout(win, timeout);
            if (zdraw_key_discard) {
                zdraw_key_discard = 0;
                return zdraw_keyboard_unknown(target, "discarded-tail");
            }
            return zdraw_keyboard_record(target);
        }
        if (zdraw_key_used == ZDRAW_KEY_BYTES) {
            zdraw_key_discard = 1;
            keypad(win, TRUE); wtimeout(win, timeout);
            return zdraw_keyboard_unknown(target, "byte-limit");
        }
    }
    keypad(win, TRUE); wtimeout(win, timeout);
    result = zdraw_query_now() >= zdraw_key_deadline;
    if (result) {
        zdraw_key_discard = 0;
        return zdraw_keyboard_unknown(target, "timeout");
    }
    return 1;
}
#endif

static void
zdraw_query_cancel(void)
{
#ifdef ZDRAW_QUERIES
    if (zdraw_query_pending >= 0)
        zdraw_query_states[zdraw_query_pending] = "cancelled";
    zdraw_query_pending = -1;
#endif
}

static void
zdraw_query_cleanup(void)
{
    int i;
    zdraw_query_cancel();
#ifdef ZDRAW_QUERIES
    for (i = 0; i < ZDRAW_QUERY_KEYS; i++) {
        if (zdraw_query_keys[i])
            define_key(NULL, zdraw_query_keys[i]);
        zdraw_query_keys[i] = 0;
    }
    zdraw_query_owner = 0;
#endif
    for (i = 0; i < 4; i++) {
        zdraw_query_states[i] = "never";
        zdraw_query_reports[i] = -1;
    }
}

static int
zccmd_query(const char *nam, char **args)
{
#ifdef ZDRAW_QUERIES
    int i, j, code, delay;
    char sequence[32], *bound;
    double now;
    if (!strcmp(args[0], "cancel") && !args[1]) {
        zdraw_query_cancel();
        return 0;
    }
    if (!strcmp(args[0], "off") && !args[1]) {
        zdraw_query_cancel();
        for (i = 0; i < ZDRAW_QUERY_KEYS; i++) {
            if (zdraw_query_keys[i] && define_key(NULL, zdraw_query_keys[i]) == ERR)
                return 1;
            zdraw_query_keys[i] = 0;
        }
        zdraw_query_owner = 0;
        return 0;
    }
    if (!strcmp(args[0], "on") && !args[1]) {
        if (zdraw_query_owner)
            return 0;
        if (!isatty(0) || !isatty(1))
            return 1;
        /* Preflight every sequence, then reserve distinct unused key codes. */
        for (i = 0; i < ZDRAW_QUERY_KEYS; i++) {
            if (i < 15)
                sprintf(sequence, "\033[?%d;%d$y", zdraw_query_modes[i / 5], i % 5);
            else
                sprintf(sequence, "\033[?%du", i - 15);
            if (key_defined(sequence))
                return 1;
        }
        for (i = 0; i < ZDRAW_QUERY_KEYS; i++) {
            for (code = KEY_MAX + 1; code < KEY_MAX + 257; code++) {
                bound = keybound(code, 0);
                if (!bound)
                    break;
                free(bound);
            }
            if (i < 15)
                sprintf(sequence, "\033[?%d;%d$y", zdraw_query_modes[i / 5], i % 5);
            else
                sprintf(sequence, "\033[?%du", i - 15);
            if (code == KEY_MAX + 257 || define_key(sequence, code) == ERR) {
                for (j = 0; j < i; j++) {
                    define_key(NULL, zdraw_query_keys[j]);
                    zdraw_query_keys[j] = 0;
                }
                return 1;
            }
            zdraw_query_keys[i] = code;
        }
        zdraw_query_owner = 1;
        return 0;
    }
    if (strcmp(args[0], "request") || !args[1] || !args[2] || args[3] ||
        zdraw_nonnegative(args[2], &delay) || delay < 20 || delay > 5000) {
        zwarnnam(nam, "query expects on, off, cancel or request capability milliseconds (20..5000)");
        return 1;
    }
    for (i = 0; i < 4; i++)
        if (!strcmp(args[1], zdraw_query_names[i]))
            break;
    if (i == 4 || !zdraw_query_owner || zdraw_query_pending >= 0 ||
        strcmp(zdraw_query_states[i], "never") || zdraw_paste_active) {
        zwarnnam(nam, "query needs an input owner, an unqueried mode and no pending request or paste");
        return 1;
    }
    now = zdraw_query_now();
    if (now < 0)
        return 1;
    /* Even an unsuccessful write might have reached the peer. Do not retry. */
    zdraw_query_states[i] = "send-error";
    if ((i == 3 ? fputs("\033[?u", stdout) : fprintf(stdout, "\033[?%d$p", zdraw_query_modes[i])) < 0 || fflush(stdout) == EOF)
        return 1;
    zdraw_query_pending = i;
    zdraw_query_deadline = now + delay;
    zdraw_query_states[i] = "pending";
    return 0;
#else
    (void)nam; (void)args;
    return 2;
#endif
}

/* Passive records. An override changes this returned record only, never the
 * stored observation or module/terminal state. Missing evidence stays unknown. */
static int
zccmd_capabilities(const char *nam, char **args)
{
    static const char *names[] = {"colors", "truecolor", "wide_text",
        "norefresh_events", "suspend_resume", "streaming_paste",
        "focus_events", "synchronized_output", "keyboard_events"};
    const char *compiled[9] = {"yes", "no", "no", "no", "no", "no", "no", "no", "no"};
    const char *support[9] = {"unknown", "unknown", "no", "no", "no", "unknown", "unknown", "unknown", "unknown"};
    const char *source[9] = {"none", "none", "compiled", "compiled", "compiled", "none", "none", "none", "none"};
    const char *enabled[9] = {"no", "no", "no", "no", "no", "no", "no", "no", "no"};
    const char *overrides[9] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
    static const char *reported[] = {"unrecognized", "set", "reset", "permanent-set", "permanent-reset"};
    LinkList info;
    int active = zdraw_getwindowbyname("stdscr") != NULL, i, j;
    char key[96], flags[16], *value, *term;
    const char *query;
    if (zdraw_association(nam, args[0]))
        return 1;
    for (i = 1; args[i]; i++) {
        value = strchr(args[i], '=');
        if (!value || (strcmp(value + 1, "yes") && strcmp(value + 1, "no") && strcmp(value + 1, "unknown")))
            return 1;
        for (j = 0; j < 9; j++)
            if (strlen(names[j]) == (size_t)(value - args[i]) && !strncmp(names[j], args[i], value - args[i]))
                break;
        if (j == 9 || overrides[j])
            return 1;
        overrides[j] = value + 1;
    }
#ifdef ZDRAW_TRUECOLOR
    compiled[1] = "yes";
    if (active) {
        support[1] = zc_truecolor_supported ? "yes" : "no";
        source[1] = "terminfo";
    }
#endif
#ifdef MULTIBYTE_SUPPORT
    compiled[2] = support[2] = "yes";
#endif
#ifdef NCURSES_VERSION
    compiled[3] = support[3] = "yes";
#endif
#ifdef ZDRAW_SUSPEND
    compiled[4] = support[4] = "yes";
#endif
#ifdef ZDRAW_PASTE
    compiled[5] = "yes";
    enabled[5] = zdraw_paste_key && !zdraw_suspended ? "yes" : "no";
#endif
#ifdef ZDRAW_ENHANCED
    compiled[6] = compiled[8] = "yes";
    enabled[6] = zdraw_focus_applied ? "yes" : "no";
    enabled[8] = zdraw_keyboard_applied ? "yes" : "no";
#endif
#ifdef ZDRAW_QUERIES
    compiled[7] = "yes";
    enabled[7] = zdraw_sync_on && !zdraw_suspended ? "yes" : "no";
#endif
    if (active) {
        support[0] = zc_has_colors ? "yes" : "no";
        source[0] = "curses";
        enabled[0] = zc_color_started && !zdraw_suspended ? "yes" : "no";
        enabled[1] = zc_truecolor && !zdraw_suspended ? "yes" : "no";
    }
    for (i = 0; i < 4; i++) {
        if (zdraw_query_reports[i] >= 0) {
            support[i + 5] = i != 3 && zdraw_query_reports[i] == 0 ? "no" : "yes";
            source[i + 5] = "reply";
        }
    }
    info = newlinklist();
    addlinknode(info, "format"); addlinknode(info, "zdraw-capabilities-1");
    addlinknode(info, "names"); addlinknode(info, "colors truecolor wide_text norefresh_events suspend_resume streaming_paste focus_events synchronized_output keyboard_events");
    addlinknode(info, "session"); addlinknode(info, active ? (zdraw_suspended ? "suspended" : "active") : "inactive");
    term = getsparam("TERM");
    addlinknode(info, "term"); addlinknode(info, term ? dupstring(term) : "");
    addlinknode(info, "locale"); addlinknode(info, dupstring(setlocale(LC_CTYPE, NULL)));
    addlinknode(info, "curses_version");
#ifdef NCURSES_VERSION
    addlinknode(info, NCURSES_VERSION);
#else
    addlinknode(info, "unknown");
#endif
    addlinknode(info, "query_owner");
#ifdef ZDRAW_QUERIES
    addlinknode(info, zdraw_query_owner ? "yes" : "no");
#else
    addlinknode(info, "no");
#endif
    for (i = 0; i < 9; i++) {
#define ZDRAW_CAP_FIELD(field, val) \
        sprintf(key, "%s,%s", names[i], field); \
        addlinknode(info, dupstring(key)); addlinknode(info, dupstring(val))
        ZDRAW_CAP_FIELD("compiled", compiled[i]);
        ZDRAW_CAP_FIELD("support", overrides[i] ? overrides[i] : support[i]);
        ZDRAW_CAP_FIELD("source", overrides[i] ? "override" : source[i]);
        ZDRAW_CAP_FIELD("evidence_support", support[i]);
        ZDRAW_CAP_FIELD("evidence_source", source[i]);
        ZDRAW_CAP_FIELD("enabled", enabled[i]);
        sprintf(flags, "%d", zdraw_query_reports[3]);
        ZDRAW_CAP_FIELD("reported", i == 8 ? (zdraw_query_reports[3] >= 0 ? flags : "unknown") :
            i >= 5 && zdraw_query_reports[i - 5] >= 0 ? reported[zdraw_query_reports[i - 5]] : "unknown");
        query = i >= 5 ? zdraw_query_states[i - 5] : "unavailable";
#ifdef ZDRAW_QUERIES
        if (i >= 5 && zdraw_query_pending == i - 5 && zdraw_query_now() >= zdraw_query_deadline)
            query = "timeout";
#endif
        ZDRAW_CAP_FIELD("query", query);
#undef ZDRAW_CAP_FIELD
    }
    return !sethparam(args[0], zdraw_list_array(info)) || (errflag & ERRFLAG_ERROR);
}

#ifdef ZDRAW_QUERIES
static int
zdraw_query_event(char *target, int mode, const char *phase, int report)
{
    LinkList info = newlinklist();
    addlinknode(info, "type"); addlinknode(info, "capability");
    addlinknode(info, "source"); addlinknode(info, mode == 3 ? "kitty-query" : "decrpm");
    addlinknode(info, "name"); addlinknode(info, dupstring(zdraw_query_names[mode]));
    addlinknode(info, "phase"); addlinknode(info, dupstring(phase));
    addlinknode(info, "text"); addlinknode(info, "");
    zdraw_event_number(info, "mode", zdraw_query_modes[mode]);
    zdraw_event_number(info, "report", report);
    return !sethparam(target, zdraw_list_array(info)) || (errflag & ERRFLAG_ERROR);
}

static int
zdraw_query_expired(char *target)
{
    int mode = zdraw_query_pending;
    double now;
    if (mode < 0)
        return -1;
    now = zdraw_query_now();
    if (now >= 0 && now < zdraw_query_deadline)
        return -1;
    zdraw_query_states[mode] = "timeout";
    zdraw_query_pending = -1;
    return zdraw_query_event(target, mode, "timeout", -1);
}
#endif

/* Zsh may own SIGWINCH, so curses KEY_RESIZE alone is insufficient.
 * A geometry record does not read input, resize windows or refresh. */
static int
zdraw_pending_resize(char *target)
{
    LinkList info;
    int rows, cols, result;
    if (zdraw_terminal_size(&rows, &cols) ||
        (rows == zdraw_event_rows && cols == zdraw_event_cols))
        return -1;
    info = newlinklist();
    addlinknode(info, "type");
    addlinknode(info, "resize");
    addlinknode(info, "key");
    addlinknode(info, "RESIZE");
    addlinknode(info, "text");
    addlinknode(info, "");
    addlinknode(info, "code");
    addlinknode(info, "unknown");
    addlinknode(info, "modifiers");
    addlinknode(info, "unknown");
    addlinknode(info, "encoding");
    addlinknode(info, "none");
    addlinknode(info, "source");
    addlinknode(info, "terminal");
    zdraw_event_number(info, "rows", rows);
    zdraw_event_number(info, "columns", cols);
    result = !sethparam(target, zdraw_list_array(info)) || (errflag & ERRFLAG_ERROR);
    if (!result) {
        zdraw_event_rows = rows;
        zdraw_event_cols = cols;
    }
    return result;
}

static int
zccmd_inputinfo(const char *nam, char **args)
{
    LinkList info;
    if (zdraw_association(nam, args[0]))
        return 1;
    info = newlinklist();
    zdraw_event_number(info, "fd", isatty(0) ? 0 : -1);
    zdraw_event_number(info, "wait_ms", 20);
    zdraw_event_number(info, "suspended", zdraw_suspended);
    addlinknode(info, "queued"); addlinknode(info, "unknown");
#ifdef ZDRAW_INPUT_DELAY
    zdraw_event_number(info, "escape_delay_ms", get_escdelay());
#else
    addlinknode(info, "escape_delay_ms"); addlinknode(info, "unknown");
#endif
#ifdef ZDRAW_PASTE
    zdraw_event_number(info, "paste_enabled", zdraw_paste_key != 0);
    zdraw_event_number(info, "paste_active", zdraw_paste_active);
    zdraw_event_number(info, "paste_pending", zdraw_paste_match);
#else
    zdraw_event_number(info, "paste_enabled", 0);
    zdraw_event_number(info, "paste_active", 0);
    zdraw_event_number(info, "paste_pending", 0);
#endif
    return !sethparam(args[0], zdraw_list_array(info)) || (errflag & ERRFLAG_ERROR);
}

#ifdef ZDRAW_PASTE
static int
zdraw_paste_record(char *target, const char *phase, char *data, int len)
{
    LinkList info = newlinklist();
    addlinknode(info, "type"); addlinknode(info, "paste");
    addlinknode(info, "phase"); addlinknode(info, dupstring(phase));
    addlinknode(info, "text"); addlinknode(info, metafy(data, len, META_HEAPDUP));
    addlinknode(info, "encoding"); addlinknode(info, "byte");
    addlinknode(info, "source"); addlinknode(info, "bracketed-paste");
    zdraw_event_number(info, "bytes", len);
    return !sethparam(target, zdraw_list_array(info)) || (errflag & ERRFLAG_ERROR);
}

static int
zdraw_paste_read(WINDOW *win, int timeout, char *target)
{
    static const char marker[] = "\033[201~";
    char data[4096];
    int used = 0, reads = 0, ch, ended = 0;
    keypad(win, FALSE);
    /* Wait only for the first byte. Never wait to fill a streaming chunk. */
    while (used <= (int)sizeof(data) - 6 && reads++ < 4096) {
        ch = wgetch(win);
        wtimeout(win, 0);
        if (ch == ERR)
            break;
        if (ch > 255)
            break; /* e.g. KEY_RESIZE: geometry is delivered on the next call. */
        if (ch == marker[zdraw_paste_match]) {
            if (++zdraw_paste_match == 6) {
                zdraw_paste_match = zdraw_paste_active = 0;
                ended = 1;
                break;
            }
        } else {
            memcpy(data + used, marker, zdraw_paste_match);
            used += zdraw_paste_match;
            zdraw_paste_match = 0;
            if (ch == '\033')
                zdraw_paste_match = 1;
            else
                data[used++] = (char)ch;
        }
    }
    wtimeout(win, timeout);
    keypad(win, TRUE);
    if (!used && !ended)
        return 1;
    return zdraw_paste_record(target, ended ? "end" : "data", data, used);
}
#endif

static int
zccmd_event(const char *nam, char **args)
{
    LinkNode node;
    LinkList info;
    ZCWin w;
    WINDOW *input_window;
    struct zdraw_input input;
    const struct zdraw_namenumberpair *key;
    char digits[DIGBUFSIZE], *keyname = "", *type = "character";
    int mouse = 0, norefresh = 0, poll = 0, result, i;
    if (zdraw_association(nam, args[1]))
        return 1;
    for (i = 2; args[i]; i++) {
        if (!strcmp(args[i], "mouse") && !mouse)
            mouse = 1;
        else if (!strcmp(args[i], "norefresh") && !norefresh)
            norefresh = 1;
        else if (!strcmp(args[i], "poll") && !poll)
            poll = 1;
        else {
            zwarnnam(nam, "event expects distinct mouse, norefresh and/or poll flags");
            return 1;
        }
    }
#ifndef NCURSES_VERSION
    if (norefresh)
        return 2;
#endif
#ifndef NCURSES_MOUSE_VERSION
    if (mouse)
        return 2;
#endif
    node = zdraw_validate_window(args[0], ZDRAW_USED);
    if (!node) {
        zwarnnam(nam, "%s: %s", zdraw_strerror(zc_errno), args[0]);
        return 1;
    }
    result = zdraw_pending_resize(args[1]);
    if (result >= 0)
        return result;
#ifdef ZDRAW_QUERIES
    result = zdraw_query_expired(args[1]);
    if (result >= 0)
        return result;
#endif
    w = (ZCWin)getdata(node);
    input_window = w->win;
#ifdef NCURSES_VERSION
    if (norefresh) {
        if (!zdraw_input_pad)
            zdraw_input_pad = newpad(1, 1);
        if (!zdraw_input_pad) {
            zwarnnam(nam, "failed to create input pad");
            return 1;
        }
        wtimeout(zdraw_input_pad, w->timeout);
        input_window = zdraw_input_pad;
    }
#endif
    if (poll)
        wtimeout(input_window, 0);
#ifdef ZDRAW_QUERIES
    else if (zdraw_query_pending >= 0) {
        double remaining = zdraw_query_deadline - zdraw_query_now();
        int wait = remaining > 5000 ? 0 : remaining > 0 ? (int)remaining + 1 : 0;
        if (w->timeout < 0 || wait < w->timeout)
            wtimeout(input_window, wait);
    }
#endif
#ifdef ZDRAW_PASTE
    if (zdraw_paste_active) {
        result = zdraw_paste_read(input_window, poll ? 0 : w->timeout, args[1]);
        wtimeout(input_window, w->timeout);
        return result;
    }
#endif
#ifdef ZDRAW_ENHANCED
    if (zdraw_keyboard_on && (zdraw_key_used || zdraw_key_discard)) {
        result = zdraw_keyboard_read(input_window, poll ? 0 : w->timeout, args[1], 0);
        wtimeout(input_window, w->timeout);
        return result;
    }
#endif
    result = zdraw_read_input(nam, input_window, mouse ? 4 : 3, &input);
    wtimeout(input_window, w->timeout);
    if (result) {
#ifdef ZDRAW_QUERIES
        result = zdraw_query_expired(args[1]);
        if (result >= 0)
            return result;
#endif
        result = zdraw_pending_resize(args[1]);
        return result >= 0 ? result : 1;
    }
#ifdef ZDRAW_PASTE
    if (zdraw_paste_key && input.key == zdraw_paste_key) {
        zdraw_paste_active = 1;
        zdraw_paste_match = 0;
        return zdraw_paste_record(args[1], "begin", "", 0);
    }
#endif
#ifdef ZDRAW_QUERIES
    if (zdraw_query_owner) {
        for (i = 0; i < ZDRAW_QUERY_KEYS; i++) {
            if (input.key == zdraw_query_keys[i]) {
                int mode = i < 15 ? i / 5 : 3, report = i < 15 ? i % 5 : i - 15;
                const char *phase = !strcmp(zdraw_query_states[mode], "never") ? "unsolicited" : "late";
                double now = zdraw_query_now();
                if (mode == zdraw_query_pending && now >= 0 && now < zdraw_query_deadline) {
                    phase = "reply";
                    zdraw_query_reports[mode] = report;
                    zdraw_query_states[mode] = "replied";
                    zdraw_query_pending = -1;
                }
                return zdraw_query_event(args[1], mode, phase, report);
            }
        }
    }
#endif
#ifdef ZDRAW_ENHANCED
    if (zdraw_focus_on && (input.key == zdraw_focus_keys[0] || input.key == zdraw_focus_keys[1]))
        return zdraw_focus_event(args[1], input.key == zdraw_focus_keys[0]);
    if (zdraw_keyboard_on && input.key < 0 && input.code == 27) {
        result = zdraw_keyboard_read(input_window, poll ? 0 : w->timeout, args[1], 1);
        wtimeout(input_window, w->timeout);
        if (result >= 0) return result;
    }
#endif
    info = newlinklist();
    addlinknode(info, "source");
    addlinknode(info, "curses");
    if (input.key >= 0) {
        type = "key";
        for (key = keypad_names; key->name; key++) {
            if (key->number == input.key) {
                keyname = dupstring(key->name);
                break;
            }
        }
        if (!*keyname) {
            if (input.key >= KEY_F(0) && input.key <= KEY_F(63))
                sprintf(digits, "F%d", input.key - KEY_F(0));
            else
                sprintf(digits, "%d", input.key);
            keyname = dupstring(digits);
        }
#ifdef KEY_RESIZE
        if (input.key == KEY_RESIZE) {
            int rows, cols;
            type = "resize";
            getmaxyx(stdscr, rows, cols);
            zdraw_event_number(info, "rows", rows);
            zdraw_event_number(info, "columns", cols);
        }
#endif
    }
#ifdef NCURSES_MOUSE_VERSION
    if (mouse && input.key == KEY_MOUSE) {
        MEVENT event;
        const struct zdraw_mouse_event *entry;
        char *button_names[sizeof(zdraw_mouse_map) / sizeof(*zdraw_mouse_map)];
        char *modifiers[4];
        int n = 0;
        if (getmouse(&event) == ERR)
            return 1;
        type = "mouse";
        zdraw_event_number(info, "id", event.id);
        zdraw_event_number(info, "x", event.x);
        zdraw_event_number(info, "y", event.y);
        zdraw_event_number(info, "z", event.z);
        for (entry = zdraw_mouse_map; entry->button; entry++) {
            if (event.bstate & entry->event) {
                sprintf(digits, "%s%d", zdraw_mouse_event_list[entry->what].name,
                        entry->button);
                button_names[n++] = dupstring(digits);
            }
        }
        button_names[n] = NULL;
        addlinknode(info, "buttons");
        addlinknode(info, zjoin(button_names, ' ', 1));
        n = 0;
        if (event.bstate & BUTTON_SHIFT)
            modifiers[n++] = "SHIFT";
        if (event.bstate & BUTTON_CTRL)
            modifiers[n++] = "CTRL";
        if (event.bstate & BUTTON_ALT)
            modifiers[n++] = "ALT";
        modifiers[n] = NULL;
        addlinknode(info, "modifiers");
        addlinknode(info, zjoin(modifiers, ' ', 1));
    } else
#endif
    {
        addlinknode(info, "modifiers");
        addlinknode(info, "unknown");
    }
    addlinknode(info, "type");
    addlinknode(info, type);
    addlinknode(info, "text");
    addlinknode(info, input.text);
    addlinknode(info, "key");
    addlinknode(info, keyname);
    zdraw_event_number(info, "code", input.code);
    addlinknode(info, "encoding");
#ifdef HAVE_WGET_WCH
    addlinknode(info, "multibyte");
#else
    addlinknode(info, "byte");
#endif
    return !sethparam(args[1], zdraw_list_array(info)) || (errflag & ERRFLAG_ERROR);
}

static int
zccmd_colorinfo(const char *nam, char **args)
{
    Param pm;
    LinkList info;
    int initialized = zdraw_getwindowbyname("stdscr") != NULL;
    zlong colors = -1, pairs = -1, color_limit = -1, pair_limit = -1;
    zlong bg_limit = -1, query_limit = -1, spans_limit = -1;

    /* Restrict assignment to a plain association: a special hash can run
     * setters with effects unrelated to this informational operation. */
    if (!isident(args[0]) || strchr(args[0], '[')) {
	zwarnnam(nam, "colorinfo expects an associative parameter name");
	return 1;
    }
    pm = (Param)gethashnode2(paramtab, args[0]);
    if (pm && ((pm->node.flags & (PM_READONLY|PM_SPECIAL)) ||
	       PM_TYPE(pm->node.flags) != PM_HASHED)) {
	zwarnnam(nam, "colorinfo expects an ordinary writable associative parameter: %s", args[0]);
	return 1;
    }

    if (initialized) {
	color_limit = pair_limit = 0;
	if (zc_color_started) {
	    colors = COLORS;
	    pairs = COLOR_PAIRS;
	    if (colors > 0)
		color_limit = colors <= (zlong)SHRT_MAX ? colors : (zlong)SHRT_MAX + 1;
	    pair_limit = zdraw_pair_limit();
	}
	bg_limit = query_limit = pair_limit;
#ifdef ZDRAW_WIDE_SPANS
	spans_limit = pair_limit;
#elif defined(HAVE_WADDCHNSTR)
	spans_limit = pair_limit < PAIR_NUMBER(A_COLOR) ? pair_limit : PAIR_NUMBER(A_COLOR);
#else
	spans_limit = 0;
#endif
#ifndef HAVE_SETCCHAR
	/* bg encodes the pair in chtype and also has a legacy 255 guard. */
	if (bg_limit > 255)
	    bg_limit = 255;
	if (bg_limit > PAIR_NUMBER(A_COLOR))
	    bg_limit = PAIR_NUMBER(A_COLOR);
#endif
#if !defined(HAVE_WIN_WCH) || !defined(HAVE_GETCCHAR)
	if (query_limit > PAIR_NUMBER(A_COLOR))
	    query_limit = PAIR_NUMBER(A_COLOR);
#endif
    }

    info = newlinklist();
    zdraw_colorinfo_value(info, "initialized", initialized);
    zdraw_colorinfo_value(info, "truecolor_supported", initialized ? zc_truecolor_supported : -1);
    zdraw_colorinfo_value(info, "truecolor_enabled", initialized ? zc_truecolor : -1);
    zdraw_colorinfo_value(info, "rgb_min", initialized ? zc_rgb_min : -1);
    zdraw_colorinfo_value(info, "rgb_max", initialized && zc_truecolor_supported ? 0xffffff : -1);
    zdraw_colorinfo_value(info, "has_colors", initialized ? zc_has_colors : -1);
    zdraw_colorinfo_value(info, "color_started", initialized ? zc_color_started : -1);
    zdraw_colorinfo_value(info, "default_colors", initialized ? zc_default_colors : -1);
    zdraw_colorinfo_value(info, "can_change_color", initialized ? zc_can_change_color : -1);
    zdraw_colorinfo_value(info, "colors", colors);
    zdraw_colorinfo_value(info, "color_pairs", pairs);
    zdraw_colorinfo_value(info, "color_limit", color_limit);
    zdraw_colorinfo_value(info, "pair_limit", pair_limit);
    zdraw_colorinfo_value(info, "bg_pair_limit", bg_limit);
    zdraw_colorinfo_value(info, "query_pair_limit", query_limit);
    zdraw_colorinfo_value(info, "spans_pair_limit", spans_limit);
    zdraw_colorinfo_value(info, "pairs_used", initialized ? next_cp : -1);
    zdraw_colorinfo_value(info, "pairs_free", initialized ? pair_limit - next_cp : -1);

    return !sethparam(args[0], zdraw_list_array(info)) || (errflag & ERRFLAG_ERROR);
}


/* Explicit per-call policy; never change the shell locale or module defaults.
 * The optional Unicode policy has a one-MiB original-byte bound. */
static int
zdraw_text_policy(const char *nam, const char *policy, char *text, int *grapheme)
{
    *grapheme = 0;
    if (!policy || !strcmp(policy, "cell") || !strcmp(policy, ZDRAW_CELL_POLICY))
        return 0;
    if (!strcmp(policy, ZDRAW_SAFE_POLICY)) {
#ifndef ZDRAW_SAFE_GRAPHEME
        return 2;
#endif
        *grapheme = 2;
    } else if (!strcmp(policy, "grapheme")) {
        *grapheme = 1;
    } else {
        zwarnnam(nam, "unsupported text policy: %s", policy);
        return 2;
    }
#ifdef ZDRAW_GRAPHEME
    {
        const char *codeset = nl_langinfo(CODESET);
        size_t bytes = 0;
        if (!isset(MULTIBYTE) || (strcmp(codeset, "UTF-8") && strcmp(codeset, "UTF8")))
            return 2;
        while (*text) {
            if (++bytes > ZDRAW_SAFE_BYTES) {
                zwarnnam(nam, "grapheme text exceeds the one-MiB byte limit");
                return 1;
            }
            if (*text++ == Meta) text++;
        }
        return 0;
    }
#else
    (void)text;
    return 2;
#endif
}

/* Headless discovery describes only policies usable by queries AND drawing. */
static int
zccmd_textpolicy(const char *nam, char **args)
{
    LinkList info;
    int policy, available, result;
    const char *locale = setlocale(LC_CTYPE, NULL);
    if (zdraw_association(nam, args[0])) return 1;
    result = zdraw_text_policy(nam, args[1], "", &policy);
    if (result) return result;
    if (policy == 1) return 2;
    available = !zdraw_text_policy(nam, ZDRAW_SAFE_POLICY, "", &result);
    info = newlinklist();
    addlinknode(info, "policy");
    addlinknode(info, policy == 2 ? ZDRAW_SAFE_POLICY : ZDRAW_CELL_POLICY);
    addlinknode(info, "default_policy"); addlinknode(info, ZDRAW_CELL_POLICY);
    addlinknode(info, "grapheme_policy"); addlinknode(info, ZDRAW_SAFE_POLICY);
    zdraw_colorinfo_value(info, "grapheme_available", available);
#ifdef ZDRAW_SAFE_GRAPHEME
    zdraw_colorinfo_value(info, "grapheme_compiled", 1);
#else
    zdraw_colorinfo_value(info, "grapheme_compiled", 0);
#endif
    addlinknode(info, "width_model"); addlinknode(info, "sum-libc-wcwidth");
    addlinknode(info, "locale"); addlinknode(info, dupstring(locale ? locale : ""));
    addlinknode(info, "boundaries");
    addlinknode(info, policy == 2 ? "unicode-egc-attach-zero" : "spacing-attach-zero");
    addlinknode(info, "unicode_version");
    addlinknode(info, policy == 2 ? "17.0.0" : "none");
    addlinknode(info, "style_policy");
    addlinknode(info, policy == 2 ? "first-scalar" : "per-span");
    zdraw_colorinfo_value(info, "emoji_two_cells", 0);
    zdraw_colorinfo_value(info, "intra_grapheme_styles", 0);
    zdraw_colorinfo_value(info, "persistent_grapheme_metadata", 0);
    zdraw_colorinfo_value(info, "native_storage_checked", policy == 2);
    addlinknode(info, "operations");
    addlinknode(info, "textinfo textpos spans spansclip string");
    zdraw_colorinfo_value(info, "max_bytes", policy == 2 ? ZDRAW_SAFE_BYTES : -1);
    zdraw_colorinfo_value(info, "max_spans", policy == 2 ? ZDRAW_SAFE_SPANS : -1);
#ifdef ZDRAW_SAFE_GRAPHEME
    zdraw_colorinfo_value(info, "native_cell_scalar_limit", CCHARW_MAX - 1);
#else
    zdraw_colorinfo_value(info, "native_cell_scalar_limit", -1);
#endif
    return !sethparam(args[0], zdraw_list_array(info)) || (errflag & ERRFLAG_ERROR);
}

/* Headless width/clip query. Validation, including the discarded suffix,
 * precedes assignment, so invalid text cannot be hidden behind a small budget. */
static int
zccmd_textinfo(const char *nam, char **args)
{
    Param pm;
    LinkList info;
    char *str = args[1], *end = str, *prefix, *before;
    int budget = INT_MAX, width = 0, total = 0, keeping = 1, have_base = 0;
    int cw, result, wide = 0, grapheme = 0, boundary, group_width = 0;
#ifdef ZDRAW_GRAPHEME
    struct zdraw_grapheme_state state = {0};
#endif
#ifdef ZDRAW_SAFE_GRAPHEME
    struct zdraw_safe_group native = {{0}, 0, 0};
#endif
    size_t bytes;
    convchar_t wc;

    if (!isident(args[0]) || strchr(args[0], '[')) {
	zwarnnam(nam, "textinfo expects an associative parameter name");
	return 1;
    }
    pm = (Param)gethashnode2(paramtab, args[0]);
    if (pm && ((pm->node.flags & (PM_READONLY|PM_SPECIAL)) ||
	       PM_TYPE(pm->node.flags) != PM_HASHED)) {
	zwarnnam(nam, "textinfo expects an ordinary writable associative parameter: %s", args[0]);
	return 1;
    }
    if (args[2] && zdraw_nonnegative(args[2], &budget)) {
	zwarnnam(nam, "textinfo expects a nonnegative decimal column budget");
	return 1;
    }
    result = zdraw_text_policy(nam, args[2] ? args[3] : NULL, args[1], &grapheme);
    if (result) return result;
#ifdef MULTIBYTE_SUPPORT
    wide = 1;
#endif
    MB_METACHARINIT();
    for (;;) {
        before = str;
        cw = 0;
        boundary = 0;
        if (*str) {
            result = zdraw_text_next(&str, wide, &wc, &cw);
            if (result || (!cw && !have_base) || total > INT_MAX - cw) {
                zwarnnam(nam, "textinfo requires printable text with a spacing character before zero-width characters");
                return result == 2 ? 2 : 1;
            }
            boundary = cw != 0;
#ifdef ZDRAW_GRAPHEME
            if (grapheme) {
                boundary = zdraw_text_break(&state, wc, cw);
            }
#endif
        }
#ifdef ZDRAW_SAFE_GRAPHEME
        if (grapheme == 2 && zdraw_safe_scalar(&native, *before ? wc : 0, cw))
            return 2;
#endif
        if (boundary || !*before) {
            if (keeping && group_width <= budget - width) {
                width += group_width;
                end = before;
            } else keeping = 0;
            group_width = 0;
        }
        if (!*before) break;
        group_width += cw;
        total += cw;
        if (cw) have_base = 1;
    }
    bytes = end - args[1];
    prefix = zhalloc(bytes + 1);
    memcpy(prefix, args[1], bytes);
    prefix[bytes] = '\0';
    info = newlinklist();
    addlinknode(info, "text");
    addlinknode(info, prefix);
    addlinknode(info, "remainder");
    addlinknode(info, end);
    zdraw_colorinfo_value(info, "width", width);
    zdraw_colorinfo_value(info, "total_width", total);
    zdraw_colorinfo_value(info, "truncated", *end != '\0');
#ifdef ZDRAW_GRAPHEME
    if (grapheme) {
        addlinknode(info, "policy");
        addlinknode(info, grapheme == 2 ? ZDRAW_SAFE_POLICY : "grapheme");
        addlinknode(info, "unicode_version"); addlinknode(info, ZDRAW_GRAPHEME_VERSION);
    }
#endif
    return !sethparam(args[0], zdraw_list_array(info)) || (errflag & ERRFLAG_ERROR);
}

/* Point queries need only one retained range, regardless of input length.
 * Boundaries group a spacing character with following zero-width characters,
 * exactly as textinfo clips; these are not Unicode grapheme boundaries. */
static int
zccmd_textpos(const char *nam, char **args)
{
    LinkList info;
    char *str = args[1], *group = str, *before, *p;
    char *hit = NULL, *end = NULL, *prefix, *text;
    int offset, by_byte, wide = 0, cw, result, grapheme = 0, boundary;
#ifdef ZDRAW_GRAPHEME
    struct zdraw_grapheme_state state = {0};
#endif
    int bytes = 0, width = 0, group_byte = 0, group_col = 0;
    int hit_byte = 0, end_byte = 0, hit_col = 0, end_col = 0;
    int have_base = 0, at_end = 0;
#ifdef ZDRAW_SAFE_GRAPHEME
    struct zdraw_safe_group native = {{0}, 0, 0};
#endif
    size_t len;
    convchar_t wc;

    if (zdraw_association(nam, args[0]))
        return 1;
    if (!strcmp(args[2], "byte"))
        by_byte = 1;
    else if (!strcmp(args[2], "column"))
        by_byte = 0;
    else {
        zwarnnam(nam, "textpos expects byte or column");
        return 1;
    }
    if (zdraw_nonnegative(args[3], &offset)) {
        zwarnnam(nam, "textpos expects a nonnegative decimal offset");
        return 1;
    }
    result = zdraw_text_policy(nam, args[4], args[1], &grapheme);
    if (result) return result;
#ifdef MULTIBYTE_SUPPORT
    wide = 1;
#endif
    MB_METACHARINIT();
    for (;;) {
        before = str;
        cw = 0;
        if (*str) {
            result = zdraw_text_next(&str, wide, &wc, &cw);
            if (result || (!cw && !have_base) || width > INT_MAX - cw) {
                zwarnnam(nam, "textpos requires printable text with a spacing character before zero-width characters");
                return result == 2 ? 2 : 1;
            }
        }
        boundary = cw != 0;
#ifdef ZDRAW_GRAPHEME
        if (grapheme && *before) {
            boundary = zdraw_text_break(&state, wc, cw);
        }
#endif
        /* Finish the preceding group before counting the next base. Validate
         * the rest of the text even after finding the requested position. */
#ifdef ZDRAW_SAFE_GRAPHEME
        if (grapheme == 2 && zdraw_safe_scalar(&native, *before ? wc : 0, cw))
            return 2;
#endif
        if (boundary || !*before) {
            if (have_base && offset >= (by_byte ? group_byte : group_col) &&
                offset < (by_byte ? bytes : width)) {
                hit = group;
                end = before;
                hit_byte = group_byte;
                end_byte = bytes;
                hit_col = group_col;
                end_col = width;
            }
            group = before;
            group_byte = bytes;
            group_col = width;
            have_base = 1;
        }
        if (!*before)
            break;
        /* Count original bytes, not the extra Meta escapes in Zsh strings. */
        for (p = before; p < str; p++) {
            if (bytes == INT_MAX) {
                zwarnnam(nam, "textpos text exceeds the byte offset limit");
                return 1;
            }
            if (*p == Meta)
                p++;
            bytes++;
        }
        width += cw;
    }
    if (offset == (by_byte ? bytes : width)) {
        hit = end = str;
        hit_byte = end_byte = bytes;
        hit_col = end_col = width;
        at_end = 1;
    }
    if (!hit) {
        zwarnnam(nam, "textpos offset is past the end of the text");
        return 1;
    }
    len = hit - args[1];
    prefix = zhalloc(len + 1);
    memcpy(prefix, args[1], len);
    prefix[len] = '\0';
    len = end - hit;
    text = zhalloc(len + 1);
    memcpy(text, hit, len);
    text[len] = '\0';
    info = newlinklist();
    addlinknode(info, "prefix");
    addlinknode(info, prefix);
    addlinknode(info, "text");
    addlinknode(info, text);
    addlinknode(info, "remainder");
    addlinknode(info, end);
    zdraw_colorinfo_value(info, "byte_start", hit_byte);
    zdraw_colorinfo_value(info, "byte_end", end_byte);
    zdraw_colorinfo_value(info, "column_start", hit_col);
    zdraw_colorinfo_value(info, "column_end", end_col);
    zdraw_colorinfo_value(info, "total_bytes", bytes);
    zdraw_colorinfo_value(info, "total_width", width);
    zdraw_colorinfo_value(info, "at_end", at_end);
#ifdef ZDRAW_GRAPHEME
    if (grapheme) {
        addlinknode(info, "policy");
        addlinknode(info, grapheme == 2 ? ZDRAW_SAFE_POLICY : "grapheme");
        addlinknode(info, "unicode_version"); addlinknode(info, ZDRAW_GRAPHEME_VERSION);
    }
#endif
    return !sethparam(args[0], zdraw_list_array(info)) || (errflag & ERRFLAG_ERROR);
}

#define ZDRAW_WRAP_BYTES 1048576
#define ZDRAW_WRAP_LINES 4096

/* Output ranges refer to original bytes and unwrapped columns. The strings
 * remain metafied until normal parameter assignment; do not re-encode them. */
static void
zdraw_wrap_line(LinkList info, int index, char *start, char *end,
                int byte_start, int byte_end, int column_start, int column_end)
{
    static const char *fields[] = {
        "byte_start", "byte_end", "column_start", "column_end", "width"
    };
    int values[5], i;
    char key[64], *text;
    size_t len = end - start;
    text = zhalloc(len + 1);
    memcpy(text, start, len);
    text[len] = '\0';
    sprintf(key, "%d,text", index);
    addlinknode(info, dupstring(key));
    addlinknode(info, text);
    values[0] = byte_start;
    values[1] = byte_end;
    values[2] = column_start;
    values[3] = column_end;
    values[4] = column_end - column_start;
    for (i = 0; i < 5; i++) {
        sprintf(key, "%d,%s", index, fields[i]);
        zdraw_event_number(info, key, values[i]);
    }
}

/* Greedy column wrapping of one printable logical line. A boundary is emitted
 * only before a spacing character, keeping every zero-width suffix with its
 * base. Partial results never reach the destination parameter. */
static int
zccmd_textwrap(const char *nam, char **args)
{
    LinkList info;
    char *str = args[1], *start = str, *before, *p;
    int budget, cw, result, wide = 0, nlines = 0;
    int bytes = 0, width = 0, start_byte = 0, start_column = 0;
    convchar_t wc;
    if (zdraw_association(nam, args[0]))
        return 1;
    if (zdraw_nonnegative(args[2], &budget) || !budget) {
        zwarnnam(nam, "textwrap expects a positive decimal column budget");
        return 1;
    }
    info = newlinklist();
#ifdef MULTIBYTE_SUPPORT
    wide = 1;
#endif
    MB_METACHARINIT();
    while (*str) {
        before = str;
        result = zdraw_text_next(&str, wide, &wc, &cw);
        if (result || (!cw && !width) || width > INT_MAX - cw) {
            zwarnnam(nam, "textwrap requires printable text with a spacing character before zero-width characters");
            return result == 2 ? 2 : 1;
        }
        if (cw > budget) {
            zwarnnam(nam, "textwrap character exceeds the column budget");
            return 1;
        }
        if (cw > budget - (width - start_column)) {
            /* Reserve a slot for the nonempty line now starting. */
            if (nlines >= ZDRAW_WRAP_LINES - 1) {
                zwarnnam(nam, "textwrap exceeds the line limit");
                return 1;
            }
            zdraw_wrap_line(info, nlines++, start, before, start_byte, bytes,
                            start_column, width);
            start = before;
            start_byte = bytes;
            start_column = width;
        }
        for (p = before; p < str; p++) {
            if (bytes == ZDRAW_WRAP_BYTES) {
                zwarnnam(nam, "textwrap exceeds the source byte limit");
                return 1;
            }
            if (*p == Meta)
                p++;
            bytes++;
        }
        width += cw;
    }
    zdraw_wrap_line(info, nlines++, start, str, start_byte, bytes,
                    start_column, width);
    addlinknode(info, "format");
    addlinknode(info, "zdraw-textwrap-1");
    zdraw_event_number(info, "columns", budget);
    zdraw_event_number(info, "line_count", nlines);
    zdraw_event_number(info, "total_bytes", bytes);
    zdraw_event_number(info, "total_width", width);
    zdraw_event_number(info, "byte_limit", ZDRAW_WRAP_BYTES);
    zdraw_event_number(info, "line_limit", ZDRAW_WRAP_LINES);
    return !sethparam(args[0], zdraw_list_array(info)) || (errflag & ERRFLAG_ERROR);
}

static int
zccmd_resize(const char *nam, char **args)
{
#ifdef HAVE_RESIZE_TERM
    int y, x, do_endwin=0, do_save=1;
    LinkNode stdscr_win = zdraw_getwindowbyname("stdscr");

    if (stdscr_win) {
        y = atoi(args[0]);
        x = atoi(args[1]);
        if (args[2]) {
            if (0 == strcmp(args[2], "endwin")) {
                do_endwin=1;
            } else if (0 == strcmp(args[2], "endwin_nosave")) {
                do_endwin=1;
                do_save=0;
            } else if (0 == strcmp(args[2], "nosave")) {
                do_save=0;
            } else {
                zwarnnam(nam, "`resize' expects `endwin', `nosave' or `endwin_nosave' for third argument, if given");
            }
        }

        if (y == 0 && x == 0 && args[2] == NULL) {
            // Special case to just test that curses has resize_term. #ifdef
            // HAVE_RESIZE_TERM will result in return value 2 if resize_term
            // is not available.
            return 0;
        } else {
            // Without this call some window moves are inaccurate. Tested on
            // OS X ncurses 5.4, Homebrew ncursesw 6.0-2, Arch Linux ncursesw
            // 6.0, Ubuntu 14.04 ncurses 5.9, FreeBSD ncursesw.so.8
            //
            // On the other hand, the whole resize goal can be (from tests)
            // accomplished by calling endwin and refresh. But to secure any
            // future problems, resize_term is provided, and it is featured
            // with endwin, so that users have multiple options.
            if (do_endwin) {
                endwin();
            }

            if( resize_term( y, x ) == OK ) {
                // Things work without this, but we need to get out from
                // endwin (i.e. call refresh), and in theory store new
                // curses state (the resize might have changed it), which
                // should be presented to terminal only after refresh.
                if (do_endwin || do_save) {
                    ZCWin w;
                    w = (ZCWin)getdata(stdscr_win);
                    wnoutrefresh(w->win);
                    doupdate();
                }

                if (do_save) {
                    gettyinfo(&curses_tty_state);
                }
                return 0;
            } else {
                return 1;
            }
        }
    } else {
        return 1;
    }
#else
    return 2;
#endif
}

/*********************
  Main builtin handler
 *********************/

/**/
static int
bin_zdraw(char *nam, char **args, UNUSED(Options ops), UNUSED(int func))
{
    char **saargs;
    const struct zdraw_subcommand *zcsc;
    int num_args;

    static const struct zdraw_subcommand scs[] = {
	{"init", zccmd_init, 0, 0},
	{"addwin", zccmd_addwin, 5, 6},
        {"addpad", zccmd_addpad, 3, 3},
        {"resizepad", zccmd_resizepad, 3, 3},
        {"movewin", zccmd_movewin, 3, 3},
        {"resizewin", zccmd_resizewin, 3, 5},
        {"treewin", zccmd_treewin, 5, 5},
        {"viewport", zccmd_viewport, 7, 7},
        {"stage", zccmd_stage, 1, -1},
        {"present", zccmd_present, 0, 0},
        {"sync", zccmd_sync, 1, 1},
	{"delwin", zccmd_delwin, 1, 1},
	{"refresh", zccmd_refresh, 0, -1},
	{"move", zccmd_move, 3, 3},
	{"clear", zccmd_clear, 1, 2},
	{"position", zccmd_position, 2, 2},
	{"geometry", zccmd_geometry, 1, 1},
	{"colorinfo", zccmd_colorinfo, 1, 1},
        {"resourceinfo", zccmd_resourceinfo, 1, 1},
        {"raster", zccmd_raster, 2, -1},
	{"textinfo", zccmd_textinfo, 2, 4},
        {"textpolicy", zccmd_textpolicy, 1, 2},
        {"textpos", zccmd_textpos, 4, 5},
        {"textwrap", zccmd_textwrap, 3, 3},
	{"truecolor", zccmd_truecolor, 1, 1},
	{"char", zccmd_char, 2, 2},
	{"string", zccmd_string, 2, 3},
	{"spans", zccmd_spans, 5, -1},
        {"fill", zccmd_fill, 7, 7},
        {"copy", zccmd_copy, 8, 8},
        {"overlay", zccmd_overlay, 8, 8},
        {"restyle", zccmd_restyle, 6, 6},
        {"prepare", zccmd_prepare, 3, -1},
        {"draw", zccmd_draw, 4, 5},
        {"unprepare", zccmd_unprepare, 1, 1},
        {"rowinfo", zccmd_rowinfo, 2, 2},
	{"spansclip", zccmd_spansclip, 6, -1},
	{"border", zccmd_border, 1, 9},
	{"end", zccmd_endwin, 0, 0},
	{"attr", zccmd_attr, 2, -1},
	{"bg", zccmd_bg, 2, -1},
	{"scroll", zccmd_scroll, 2, 2},
	{"input", zccmd_input, 1, 4},
	{"event", zccmd_event, 2, 5},
        {"paste", zccmd_paste, 1, 1},
        {"suspend", zccmd_suspend, 0, 0},
        {"resume", zccmd_resume, 0, 0},
        {"inputinfo", zccmd_inputinfo, 1, 1},
        {"capabilities", zccmd_capabilities, 1, 10},
        {"query", zccmd_query, 1, 3},
        {"focus", zccmd_focus, 1, 2},
        {"keyboard", zccmd_keyboard, 1, 1},
        {"inputdelay", zccmd_inputdelay, 1, 1},
	{"timeout", zccmd_timeout, 2, 2},
	{"mouse", zccmd_mouse, 0, -1},
	{"querychar", zccmd_querychar, 1, 2},
        {"cellinfo", zccmd_cellinfo, 2, 2},
        {"snapshot", zccmd_snapshot, 2, 3},
	{"touch", zccmd_touch, 1, -1},
	{"resize", zccmd_resize, 2, 3},
	{NULL, (zccmd_t)0, 0, 0}
    };

    for(zcsc = scs; zcsc->name; zcsc++) {
	if(!strcmp(args[0], zcsc->name))
	    break;
    }

    if (zcsc->name == NULL) {
	zwarnnam(nam, "unknown subcommand: %s", args[0]);
	return 1;
    }

    saargs = args;
    while (*saargs++);
    num_args = saargs - (args + 2);

    if (num_args < zcsc->minargs) {
	zwarnnam(nam, "too few arguments for subcommand: %s", args[0]);
	return 1;
    } else if (zcsc->maxargs >= 0 && num_args > zcsc->maxargs) {
	zwarnnam(nam, "too many arguments for subcommand: %s", args[0]);
	return 1;
    }

    if (zdraw_suspended && zcsc->cmd != zccmd_resume &&
        zcsc->cmd != zccmd_suspend && zcsc->cmd != zccmd_endwin &&
        zcsc->cmd != zccmd_capabilities && zcsc->cmd != zccmd_resourceinfo &&
        zcsc->cmd != zccmd_inputinfo && zcsc->cmd != zccmd_geometry &&
        zcsc->cmd != zccmd_colorinfo && zcsc->cmd != zccmd_textinfo &&
        zcsc->cmd != zccmd_textpos && zcsc->cmd != zccmd_textpolicy &&
        zcsc->cmd != zccmd_textwrap) {
        zwarnnam(nam, "resume the suspended session first");
        return 1;
    }
#ifdef ZDRAW_ENHANCED
    if ((zdraw_keyboard_on || zdraw_focus_on) && zcsc->cmd == zccmd_input) {
        zwarnnam(nam, "use event while enhanced input owns decoding");
        return 1;
    }
#endif
#ifdef ZDRAW_QUERIES
    if (zdraw_query_owner && zcsc->cmd == zccmd_input) {
        zwarnnam(nam, "use event while capability queries own input");
        return 1;
    }
#endif
#ifdef ZDRAW_PASTE
    if (zdraw_paste_key && zcsc->cmd == zccmd_input) {
        zwarnnam(nam, "use event while bracketed paste owns input");
        return 1;
    }
#endif
    if (zcsc->cmd != zccmd_init && zcsc->cmd != zccmd_endwin &&
	zcsc->cmd != zccmd_geometry && zcsc->cmd != zccmd_colorinfo &&
	zcsc->cmd != zccmd_textinfo && zcsc->cmd != zccmd_textpos &&
        zcsc->cmd != zccmd_textwrap && zcsc->cmd != zccmd_textpolicy &&
        zcsc->cmd != zccmd_capabilities &&
        zcsc->cmd != zccmd_resourceinfo && zcsc->cmd != zccmd_raster &&
	!zdraw_getwindowbyname("stdscr")) {
	zwarnnam(nam, "command `%s' can't be used before `zdraw init'",
		 zcsc->name);
	return 1;
    }

    /* Pads have no input/presentation origin. Reject them before an input
     * call can consume a resize event or read from the shared queue. */
    if (zcsc->cmd == zccmd_input || zcsc->cmd == zccmd_event ||
        zcsc->cmd == zccmd_timeout || zcsc->cmd == zccmd_refresh) {
        char **arg;
        for (arg = args + 1; *arg; arg++) {
            LinkNode node = zdraw_getwindowbyname(*arg);
            if (node && (((ZCWin)getdata(node))->flags & ZCWF_PAD)) {
                zwarnnam(nam, "pads require viewport presentation and an ordinary input window: %s", *arg);
                return 1;
            }
            if (zcsc->cmd != zccmd_refresh)
                break;
        }
    }
    return zcsc->cmd(nam, args+1);
}


static struct builtin bintab[] = {
    BUILTIN("zdraw", 0, bin_zdraw, 1, -1, 0, "", NULL),
};


/*******************
 * Special variables
 *******************/

static char **
zdraw_featuresgetfn(UNUSED(Param pm))
{
    /* Keep these conditions in step with the operations they describe.
     * This is compile-time support, not terminal capability or state. */
    static char *features[] = {
	"colorinfo",
        "resource_info",
        "cell_inspection",
        "window_snapshots",
        "cell_occupancy",
        "staged_refresh",
#ifdef HAVE_MVWIN
        "window_movement",
#endif
#ifdef ZDRAW_WINDOW_RESIZE
        "window_resize",
#endif
#ifdef ZDRAW_WINDOW_TREE
        "window_trees",
#endif
#ifdef ZDRAW_PADS
        "offscreen_pads",
#endif
#ifdef ZDRAW_PAD_RESIZE
        "pad_resize",
#endif
#ifdef HAVE_WCHGAT
        "region_restyle",
#endif
#if defined(HAVE_COPYWIN) && defined(HAVE_NEWPAD)
        "region_copy",
        "transparent_copy",
#endif
#if defined(HAVE_WIN_WCH) && defined(HAVE_GETCCHAR)
        "wide_cell_inspection",
#endif
	"custom_borders",
	"textinfo",
        "text_wrapping",
        "text_positions",
        "text_policy",
#ifdef ZDRAW_SAFE_GRAPHEME
        "grapheme_safe_text",
#endif
#ifdef ZDRAW_GRAPHEME
        "grapheme_boundaries",
#endif
        "structured_events",
        "event_poll",
        "input_info",
        "capability_evidence",
#ifdef ZDRAW_QUERIES
        "capability_queries",
        "synchronized_output",
        "focus_events",
        "keyboard_events",
#endif
#ifdef ZDRAW_PASTE
        "streaming_paste",
#endif
#ifdef ZDRAW_SUSPEND
        "suspend_resume",
#endif
#ifdef ZDRAW_INPUT_DELAY
        "input_delay",
#endif
#ifdef NCURSES_VERSION
        "norefresh_events",
#endif
#if defined(TIOCGWINSZ) || defined(KEY_RESIZE)
        "resize_events",
#endif
#ifdef HAVE_WGET_WCH
        "wide_events",
#endif
#ifdef MULTIBYTE_SUPPORT
	"wide_text",
#endif
#ifdef ZDRAW_TRUECOLOR
	"truecolor",
#endif
#if defined(ZDRAW_WIDE_SPANS) || defined(HAVE_WADDCHNSTR)
	"styled_spans",
        "region_fill",
        "prepared_rows",
	"clipped_spans",
#endif
#ifdef ZDRAW_WIDE_SPANS
	"wide_spans",
#endif
#ifdef HAVE_USE_DEFAULT_COLORS
	"default_colors",
#endif
#ifdef TIOCGWINSZ
	"geometry",
#endif
#ifdef NCURSES_MOUSE_VERSION
	"mouse",
#endif
#ifdef HAVE_RESIZE_TERM
	"resize",
#endif
#if defined(HAVE_SETCCHAR) && defined(HAVE_WBORDER_SET)
	"wide_borders",
#endif
	NULL
    };

    return arrdup(features);
}

static const struct gsu_array zdraw_features_gsu =
{ zdraw_featuresgetfn, arrsetfn, stdunsetfn };


static char **
zdraw_colorsarrgetfn(UNUSED(Param pm))
{
    return zdraw_pairs_to_array(zdraw_colors);
}

static const struct gsu_array zdraw_colorsarr_gsu =
{ zdraw_colorsarrgetfn, arrsetfn, stdunsetfn };


static char **
zdraw_attrgetfn(UNUSED(Param pm))
{
    return zdraw_pairs_to_array(zdraw_attributes);
}

static const struct gsu_array zdraw_attrs_gsu =
{ zdraw_attrgetfn, arrsetfn, stdunsetfn };


static char **
zdraw_keycodesgetfn(UNUSED(Param pm))
{
    return zdraw_pairs_to_array(keypad_names);
}

static const struct gsu_array zdraw_keycodes_gsu =
{ zdraw_keycodesgetfn, arrsetfn, stdunsetfn };


static char **
zdraw_windowsgetfn(UNUSED(Param pm))
{
    LinkNode node;
    char **arr;
    int count = countlinknodes(zdraw_windows);

    arr = (char **)zhalloc((count+1) * sizeof(char *)) + count;
    *arr = NULL;

    for (node = firstnode(zdraw_windows); node; incnode(node))
	*--arr = dupstring(((ZCWin)getdata(node))->name);

    return arr;
}

static const struct gsu_array zdraw_windows_gsu =
{ zdraw_windowsgetfn, arrsetfn, stdunsetfn };


static zlong
zdraw_colorsintgetfn(UNUSED(Param pm))
{
    return COLORS;
}

static const struct gsu_integer zdraw_colorsint_gsu =
{ zdraw_colorsintgetfn, nullintsetfn, stdunsetfn };


static zlong
zdraw_colorpairsintgetfn(UNUSED(Param pm))
{
    return COLOR_PAIRS;
}

static const struct gsu_integer zdraw_colorpairsint_gsu =
{ zdraw_colorpairsintgetfn, nullintsetfn, stdunsetfn };


static struct paramdef partab[] = {
    SPECIALPMDEF("zdraw_features", PM_ARRAY|PM_READONLY,
		 &zdraw_features_gsu, NULL, NULL),
    SPECIALPMDEF("zdraw_colors", PM_ARRAY|PM_READONLY,
		 &zdraw_colorsarr_gsu, NULL, NULL),
    SPECIALPMDEF("zdraw_attrs", PM_ARRAY|PM_READONLY,
		 &zdraw_attrs_gsu, NULL, NULL),
    SPECIALPMDEF("zdraw_keycodes", PM_ARRAY|PM_READONLY,
		 &zdraw_keycodes_gsu, NULL, NULL),
    SPECIALPMDEF("zdraw_windows", PM_ARRAY|PM_READONLY,
		 &zdraw_windows_gsu, NULL, NULL),
    SPECIALPMDEF("ZDRAW_COLORS", PM_INTEGER|PM_READONLY,
		 &zdraw_colorsint_gsu, NULL, NULL),
    SPECIALPMDEF("ZDRAW_COLOR_PAIRS", PM_INTEGER|PM_READONLY,
		 &zdraw_colorpairsint_gsu, NULL, NULL)
};

/***************************
 * Standard module interface
 ***************************/


/*
 * boot_ is executed when the module is loaded.
 */

static struct features module_features = {
    bintab, sizeof(bintab)/sizeof(*bintab),
    NULL, 0,
    NULL, 0,
    partab, sizeof(partab)/sizeof(*partab),
    0
};

/**/
int
setup_(UNUSED(Module m))
{
    return 0;
}

/**/
int
features_(Module m, char ***features)
{
    *features = featuresarray(m, &module_features);
    return 0;
}

/**/
int
enables_(Module m, int **enables)
{
    return handlefeatures(m, &module_features, enables);
}

/**/
int
boot_(UNUSED(Module m))
{
    zdraw_windows = znewlinklist();

    return 0;
}

/**/
int
cleanup_(Module m)
{
    zccmd_endwin(NULL, NULL);
    freelinklist(zdraw_windows, (FreeFunc) zdraw_free_window);
    return setfeatureenables(m, &module_features, NULL);
}

/**/
int
finish_(UNUSED(Module m))
{
    return 0;
}

/* Include terminfo only after the command implementation: its unprefixed
 * capability macros (columns, lines, etc.) collide with ordinary identifiers.
 * Keep a complete SCREEN lifetime, rather than initscr's process-wide cache.
 * With --as-needed the shell can retain libtinfo while unloading libncurses;
 * leaving a SCREEN behind then makes ncurses' reloaded color globals stale.
 */
#if defined(HAVE_NEWTERM) && defined(HAVE_DELSCREEN) && \
    defined(HAVE_SET_CURTERM) && defined(ZSH_HAVE_TERM_H)
# include "../zshterm.h"
static SCREEN *zdraw_screen;
static TERMINAL *zdraw_previous_terminal;
# ifdef NCURSES_VERSION
static SCREEN *zdraw_previous_screen;
# endif
#endif

static WINDOW *
zdraw_screen_init(void)
{
#if defined(HAVE_NEWTERM) && defined(HAVE_DELSCREEN) && \
    defined(HAVE_SET_CURTERM) && defined(ZSH_HAVE_TERM_H)
    zdraw_previous_terminal = cur_term;
# ifdef NCURSES_VERSION
    /* ncurses accepts NULL here and returns the detached current SCREEN.
     * A previous owner (such as stock zsh/curses) may retain it after endwin. */
    zdraw_previous_screen = set_term(NULL);
    set_term(zdraw_previous_screen);
    set_curterm(zdraw_previous_terminal);
# endif
    zdraw_screen = newterm(NULL, stdout, stdin);
    if (!zdraw_screen) {
# ifdef NCURSES_VERSION
        set_term(zdraw_previous_screen);
        zdraw_previous_screen = NULL;
# endif
        set_curterm(zdraw_previous_terminal);
        zdraw_previous_terminal = NULL;
        return NULL;
    }
    return stdscr;
#else
    return initscr();
#endif
}

static void
zdraw_screen_end(void)
{
#if defined(HAVE_NEWTERM) && defined(HAVE_DELSCREEN) && \
    defined(HAVE_SET_CURTERM) && defined(ZSH_HAVE_TERM_H)
    if (zdraw_screen) {
        delscreen(zdraw_screen);
        zdraw_screen = NULL;
# ifdef NCURSES_VERSION
        set_term(zdraw_previous_screen);
        zdraw_previous_screen = NULL;
# endif
        set_curterm(zdraw_previous_terminal);
        zdraw_previous_terminal = NULL;
# if defined(NCURSES_VERSION) && defined(ZDRAW_WINDOW_TREE)
        /* ncurses delscreen also frees any retired windows awaiting deletion. */
        zdraw_tree_retired_count = 0;
# endif
    }
#endif
}
