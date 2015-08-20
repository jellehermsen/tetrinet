/* Tetrinet for Linux, by Andrew Church <achurch@achurch.org>
 * This program is public domain.
 *
 * Text terminal I/O routines.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <curses.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include "tetrinet.h"
#include "tetris.h"
#include "io.h"

/*************************************************************************/

#define MY_HLINE	(fancy ? ACS_HLINE : '-')
#define MY_VLINE	(fancy ? ACS_VLINE : '|')
#define MY_ULCORNER	(fancy ? ACS_ULCORNER : '+')
#define MY_URCORNER	(fancy ? ACS_URCORNER : '+')
#define MY_LLCORNER	(fancy ? ACS_LLCORNER : '+')
#define MY_LRCORNER	(fancy ? ACS_LRCORNER : '+')

#define MY_HLINE2	(fancy ? (ACS_HLINE | A_BOLD) : '=')
#define MY_BOLD		(fancy ? A_BOLD : 0)

/*************************************************************************/
/******************************* Input stuff *****************************/
/*************************************************************************/

/* Return either an ASCII code 0-255, a K_* value, or -1 if server input is
 * waiting.  Return -2 if we run out of time with no input.
 */

static int wait_for_input(int msec)
{
    fd_set fds;
    struct timeval tv;
    int c;
    static int escape = 0;

    FD_ZERO(&fds);
    FD_SET(0, &fds);
    FD_SET(server_sock, &fds);
    tv.tv_sec = msec/1000;
    tv.tv_usec = (msec*1000) % 1000000;
    while (select(server_sock+1, &fds, NULL, NULL, msec<0 ? NULL : &tv) < 0) {
	if (errno != EINTR)
	    perror("Warning: select() failed");
    }
    if (FD_ISSET(0, &fds)) {
	c = getch();
	if (!escape && c == 27) {	/* Escape */
	    escape = 1;
	    c = wait_for_input(1000);
	    escape = 0;
	    if (c < 0)
		return 27;
	    else
		return c;
	}
	if (c == KEY_UP)
	    return K_UP;
	else if (c == KEY_DOWN)
	    return K_DOWN;
	else if (c == KEY_LEFT)
	    return K_LEFT;
	else if (c == KEY_RIGHT)
	    return K_RIGHT;
	else if (c == KEY_F(1) || c == ('1'|0x80) || (escape && c == '1'))
	    return K_F1;
	else if (c == KEY_F(2) || c == ('2'|0x80) || (escape && c == '2'))
	    return K_F2;
	else if (c == KEY_F(3) || c == ('3'|0x80) || (escape && c == '3'))
	    return K_F3;
	else if (c == KEY_F(4) || c == ('4'|0x80) || (escape && c == '4'))
	    return K_F4;
	else if (c == KEY_F(5) || c == ('5'|0x80) || (escape && c == '5'))
	    return K_F5;
	else if (c == KEY_F(6) || c == ('6'|0x80) || (escape && c == '6'))
	    return K_F6;
	else if (c == KEY_F(7) || c == ('7'|0x80) || (escape && c == '7'))
	    return K_F7;
	else if (c == KEY_F(8) || c == ('8'|0x80) || (escape && c == '8'))
	    return K_F8;
	else if (c == KEY_F(9) || c == ('9'|0x80) || (escape && c == '9'))
	    return K_F9;
	else if (c == KEY_F(10) || c == ('0'|0x80) || (escape && c == '0'))
	    return K_F10;
	else if (c == KEY_F(11))
	    return K_F11;
	else if (c == KEY_F(12))
	    return K_F12;
	else if (c == KEY_BACKSPACE)
	    return 8;
	else if (c >= 0x0100)
	    return K_INVALID;
	else if (c == 7)   /* ^G */
	    return 27;  /* Escape */
	else
	    return c;
    } /* if (FD_ISSET(0, &fds)) */
    else if (FD_ISSET(server_sock, &fds))
	return -1;
    else
	return -2;	/* out of time */
}

/*************************************************************************/
/****************************** Output stuff *****************************/
/*************************************************************************/

/* Size of the screen */
static int scrwidth, scrheight;

/* Is color available? */
static int has_color;

/*************************************************************************/

/* Text buffers: */

typedef struct {
    int x, y, width, height;
    int line;
    WINDOW *win;	/* NULL if not currently displayed */
    char **text;
} TextBuffer;

static TextBuffer plinebuf, gmsgbuf, attdefbuf;

/*************************************************************************/

/* Window for typing in-game text, and its coordinates: */

static WINDOW *gmsg_inputwin;
static int gmsg_inputpos, gmsg_inputheight;

/*************************************************************************/
/*************************************************************************/

/* Clean up the screen on exit. */

static void screen_cleanup()
{
    wmove(stdscr, scrheight-1, 0);
    wrefresh(stdscr);
    endwin();
    printf("\n");
}

/*************************************************************************/

/* Little signal handler that just does an exit(1) (thereby getting our
 * cleanup routine called), except for TSTP, which does a clean suspend.
 */

static void (*old_tstp)(int sig);

static void sighandler(int sig)
{
    if (sig != SIGTSTP) {
	endwin();
	psignal(sig, "tetrinet");
	exit(1);
    }
    endwin();
    signal(SIGTSTP, old_tstp);
    raise(SIGTSTP);
    doupdate();
    signal(SIGTSTP, sighandler);
}

/*************************************************************************/
/*************************************************************************/

#define MAXCOLORS	256

static int colors[MAXCOLORS][2] = { {-1,-1} };

/* Return a color attribute value. */

static long getcolor(int fg, int bg)
{
    int i;

    if (colors[0][0] < 0) {
	start_color();
	memset(colors, -1, sizeof(colors));
	colors[0][0] = COLOR_WHITE;
	colors[0][1] = COLOR_BLACK;
    }
    if (fg == COLOR_WHITE && bg == COLOR_BLACK)
	return COLOR_PAIR(0);
    for (i = 1; i < MAXCOLORS; i++) {
	if (colors[i][0] == fg && colors[i][1] == bg)
	    return COLOR_PAIR(i);
    }
    for (i = 1; i < MAXCOLORS; i++) {
	if (colors[i][0] < 0) {
	    if (init_pair(i, fg, bg) == ERR)
		continue;
	    colors[i][0] = fg;
	    colors[i][1] = bg;
	    return COLOR_PAIR(i);
	}
    }
    return -1;
}

/*************************************************************************/
/*************************************************************************/

/* Set up the screen stuff. */

static void screen_setup(void)
{
    /* Avoid messy keyfield signals while we're setting up */
    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);

    initscr();
    cbreak();
    noecho();
    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);
    leaveok(stdscr, TRUE);
    if ((has_color = has_colors()))
	start_color();
    getmaxyx(stdscr, scrheight, scrwidth);
    scrwidth--;  /* Don't draw in last column--this can cause scroll */

    /* don't start with fewer lines */
    if (scrheight < 50)
    {
	nocbreak();
	endwin();
	fprintf(stderr, "You need at least 50 lines to play tetrinet.\n");
	exit(1);
    }

    /* Cancel all this when we exit. */
    atexit(screen_cleanup);

    /* Catch signals so we can exit cleanly. */
    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);
    signal(SIGHUP, sighandler);
    signal(SIGUSR1, sighandler);
    signal(SIGUSR2, sighandler);
    signal(SIGALRM, sighandler);
    signal(SIGTSTP, sighandler);
#ifdef SIGXCPU
    signal(SIGXCPU, sighandler);
#endif
#ifdef SIGXFSZ
    signal(SIGXFSZ, sighandler);
#endif

    /* Broken pipes don't want to bother us at all. */
    signal(SIGPIPE, SIG_IGN);
}

/*************************************************************************/

/* Redraw everything on the screen. */

static void screen_refresh(void)
{
    if (gmsg_inputwin)
	touchline(stdscr, gmsg_inputpos, gmsg_inputheight);
    if (plinebuf.win)
	touchline(stdscr, plinebuf.y, plinebuf.height);
    if (gmsgbuf.win)
	touchline(stdscr, gmsgbuf.y, gmsgbuf.height);
    if (attdefbuf.win)
	touchline(stdscr, attdefbuf.y, attdefbuf.height);
    wnoutrefresh(stdscr);
    doupdate();
}

/*************************************************************************/

/* Like screen_refresh(), but clear the screen first. */

static void screen_redraw(void)
{
    clearok(stdscr, TRUE);
    screen_refresh();
}

/*************************************************************************/
/************************* Text buffer routines **************************/
/*************************************************************************/

/* Put a line of text in a text buffer. */

static void outline(TextBuffer *buf, const unsigned char *s)
{
    if (buf->line == buf->height) {
	if (buf->win)
	    scroll(buf->win);
	memmove(buf->text, buf->text+1, (buf->height-1) * sizeof(char *));
	buf->line--;
    }
    if (buf->win) {
	int i, x = 0, l = strlen(s);

	for (i = 0; i < l; i++) {
	    unsigned char c = s[i] - 1;

	    if (c < TATTR_MAX) {
		static const int cmap[8] = {
		    COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_YELLOW,
		    COLOR_BLUE, COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE,
		};

		switch (c) {
		    case TATTR_RESET:
			wattrset(buf->win, A_NORMAL);
			break;
		    case TATTR_BOLD:
			wattron(buf->win, A_BOLD);
			break;
		    case TATTR_ITALIC:
			wattron(buf->win, A_STANDOUT);
			break;
		    case TATTR_UNDERLINE:
			wattron(buf->win, A_UNDERLINE);
			break;
		    default:
			assert(c < TATTR_CMAX);
			wattron(buf->win, getcolor(c == 0 ? COLOR_WHITE : cmap[c % 8], COLOR_BLACK)
			                  | (A_BOLD * (c / 8)));
			break;
		}

	    } else {
		mvwaddch(buf->win, buf->line, x++, c + 1);
	    }
	}

	wattrset(buf->win, A_NORMAL);
    }
    if (s != (const unsigned char *) buf->text[buf->line])   /* check for restoring display */
	buf->text[buf->line] = strdup(s);
    buf->line++;
}

static void draw_text(int bufnum, const char *s)
{
    char str[1024];	/* hopefully scrwidth < 1024 */
    const char *t;
    int indent = 0;
    int x = 0, y = 0;
    TextBuffer *buf;

    switch (bufnum) {
	case BUFFER_PLINE:  buf = &plinebuf;  break;
	case BUFFER_GMSG:   buf = &gmsgbuf;   break;
	case BUFFER_ATTDEF: buf = &attdefbuf; break;
	default: return;
    }
    if (!buf->text)
	return;
    if (buf->win) {
	getyx(stdscr, y, x);
	attrset(getcolor(COLOR_WHITE, COLOR_BLACK));
    }
    while (*s && isspace(*s))
	s++;
    while (strlen(s) > buf->width - indent) {
	t = s + buf->width - indent;
	while (t >= s && !isspace(*t))
	    t--;
	while (t >= s && isspace(*t))
	    t--;
	t++;
	if (t < s)
	    t = s + buf->width - indent;
	if (indent > 0)
	    sprintf(str, "%*s", indent, "");
	strncpy(str+indent, s, t-s);
	str[t-s+indent] = 0;
	outline(buf, str);
	indent = 2;
	while (isspace(*t))
	    t++;
	s = t;
    }
    if (indent > 0)
	sprintf(str, "%*s", indent, "");
    strcpy(str+indent, s);
    outline(buf, str);
    if (buf->win) {
	move(y, x);
	screen_refresh();
    }
}

/*************************************************************************/

/* Clear the contents of a text buffer. */

static void clear_text(int bufnum)
{
    TextBuffer *buf;
    int i;

    switch (bufnum) {
	case BUFFER_PLINE:  buf = &plinebuf;  break;
	case BUFFER_GMSG:   buf = &gmsgbuf;   break;
	case BUFFER_ATTDEF: buf = &attdefbuf; break;
	default: return;
    }
    if (buf->text) {
	for (i = 0; i < buf->height; i++) {
	    if (buf->text[i]) {
		free(buf->text[i]);
		buf->text[i] = NULL;
	    }
	}
	buf->line = 0;
    }
    if (buf->win) {
	werase(buf->win);
	screen_refresh();
    }
}

/*************************************************************************/

/* Restore the contents of the given text buffer. */

static void restore_text(TextBuffer *buf)
{
    buf->line = 0;
    while (buf->line < buf->height && buf->text[buf->line])
	outline(buf, buf->text[buf->line]);
}

/*************************************************************************/

/* Open a window for the given text buffer. */

static void open_textwin(TextBuffer *buf)
{
    if (buf->height <= 0 || buf->width <= 0) {
	char str[256];
	move(scrheight-1, 0);
	snprintf(str, sizeof(str), "ERROR: bad textwin size (%d,%d)",
			buf->width, buf->height);
	addstr(str);
	exit(1);
    }
    if (!buf->win) {
	buf->win = subwin(stdscr, buf->height, buf->width, buf->y, buf->x);
	scrollok(buf->win, TRUE);
    }
    if (!buf->text)
	buf->text = calloc(buf->height, sizeof(char *));
    else
	restore_text(buf);
}

/*************************************************************************/

/* Close the window for the given text buffer, if it's open. */

static void close_textwin(TextBuffer *buf)
{
    if (buf->win) {
	delwin(buf->win);
	buf->win = NULL;
    }
}

/*************************************************************************/
/************************ Field drawing routines *************************/
/*************************************************************************/

/* Are we on a wide screen (>=92 columns)? */
static int wide_screen = 0;

/* Field display X/Y coordinates. */
static const int own_coord[2] = {1,0};
static int other_coord[5][2] =	/* Recomputed based on screen width */
    { {30,0}, {47,0}, {64,0}, {47,24}, {64,24} };

/* Position of the status window. */
static const int status_coord[2]     = {29,25};
static const int next_coord[2]       = {41,24};
static const int alt_status_coord[2] = {29,2};
static const int alt_next_coord[2]   = {30,8};

/* Position of the attacks/defenses window. */
static const int attdef_coord[2] = {28,28};
static const int alt_attdef_coord[2] = {28,24};

/* Position of the text window.  X coordinate is ignored. */
static const int field_text_coord[2] = {0,47};

/* Information for drawing blocks.  Color attributes are added to blocks in
 * the setup_fields() routine. */
static int tile_chars[15] =
    { ' ','#','#','#','#','#','a','c','n','r','s','b','g','q','o' };

/* Are we redrawing the entire display? */
static int field_redraw = 0;

/*************************************************************************/
/*************************************************************************/

/* Set up the field display. */

static void draw_own_field(void);
static void draw_other_field(int player);
static void draw_status(void);
static void draw_specials(void);
static void draw_gmsg_input(const char *s, int pos);

static void setup_fields(void)
{
    int i, j, x, y, base, delta, attdefbot;
    char buf[32];

    if (!(tile_chars[0] & A_ATTRIBUTES)) {
	for (i = 1; i < 15; i++)
	    tile_chars[i] |= A_BOLD;
	tile_chars[1] |= getcolor(COLOR_BLUE, COLOR_BLACK);
	tile_chars[2] |= getcolor(COLOR_YELLOW, COLOR_BLACK);
	tile_chars[3] |= getcolor(COLOR_GREEN, COLOR_BLACK);
	tile_chars[4] |= getcolor(COLOR_MAGENTA, COLOR_BLACK);
	tile_chars[5] |= getcolor(COLOR_RED, COLOR_BLACK);
    }

    field_redraw = 1;
    leaveok(stdscr, TRUE);
    close_textwin(&plinebuf);
    clear();
    attrset(getcolor(COLOR_WHITE,COLOR_BLACK));

    if (scrwidth >= 92) {
	wide_screen = 1;
	base = 41;
    } else {
	base = 28;
    }
    delta = (scrwidth - base) / 3;
    base += 2 + (delta - (FIELD_WIDTH+5)) / 2;
    other_coord[0][0] = base;
    other_coord[1][0] = base + delta;
    other_coord[2][0] = base + delta*2;
    other_coord[3][0] = base + delta;
    other_coord[4][0] = base + delta*2;

    attdefbot = field_text_coord[1] - 1;
    if (scrheight - field_text_coord[1] > 3) {
	move(field_text_coord[1], 0);
	hline(MY_HLINE2, scrwidth);
	attdefbot--;
	if (scrheight - field_text_coord[1] > 5) {
	    move(scrheight-2, 0);
	    hline(MY_HLINE2, scrwidth);
	    attrset(MY_BOLD);
	    move(scrheight-1, 0);
	    addstr("F1=Show Fields  F2=Partyline  F3=Winlist");
	    move(scrheight-1, scrwidth-8);
	    addstr("F10=Quit");
	    attrset(A_NORMAL);
	    gmsgbuf.y = field_text_coord[1]+1;
	    gmsgbuf.height = scrheight - field_text_coord[1] - 3;
	} else {
	    gmsgbuf.y = field_text_coord[1]+1;
	    gmsgbuf.height = scrheight - field_text_coord[1] - 1;
	}
    } else {
	gmsgbuf.y = field_text_coord[1];
	gmsgbuf.height = scrheight - field_text_coord[1];
    }
    gmsgbuf.x = field_text_coord[0];
    gmsgbuf.width = scrwidth;
    open_textwin(&gmsgbuf);

    x = own_coord[0];
    y = own_coord[1];
    sprintf(buf, "%d", my_playernum);
    mvaddstr(y, x-1, buf);
    for (i = 2; i < FIELD_HEIGHT*2 && players[my_playernum-1][i-2]; i++)
	mvaddch(y+i, x-1, players[my_playernum-1][i-2]);
    if (teams[my_playernum-1] != '\0') {
	mvaddstr(y, x+FIELD_WIDTH*2+2, "T");
	for (i = 2; i < FIELD_HEIGHT*2 && teams[my_playernum-1][i-2]; i++)
	    mvaddch(y+i, x+FIELD_WIDTH*2+2, teams[my_playernum-1][i-2]);
    }
    move(y, x);
    vline(MY_VLINE, FIELD_HEIGHT*2);
    move(y, x+FIELD_WIDTH*2+1);
    vline(MY_VLINE, FIELD_HEIGHT*2);
    move(y+FIELD_HEIGHT*2, x);
    addch(MY_LLCORNER);
    hline(MY_HLINE, FIELD_WIDTH*2);
    move(y+FIELD_HEIGHT*2, x+FIELD_WIDTH*2+1);
    addch(MY_LRCORNER);
    mvaddstr(y+FIELD_HEIGHT*2+2, x, "Specials:");
    draw_own_field();
    draw_specials();

    for (j = 0; j < 5; j++) {
	x = other_coord[j][0];
	y = other_coord[j][1];
	move(y, x);
	vline(MY_VLINE, FIELD_HEIGHT);
	move(y, x+FIELD_WIDTH+1);
	vline(MY_VLINE, FIELD_HEIGHT);
	move(y+FIELD_HEIGHT, x);
	addch(MY_LLCORNER);
	hline(MY_HLINE, FIELD_WIDTH);
	move(y+FIELD_HEIGHT, x+FIELD_WIDTH+1);
	addch(MY_LRCORNER);
	if (j+1 >= my_playernum) {
	    sprintf(buf, "%d", j+2);
	    mvaddstr(y, x-1, buf);
	    if (players[j+1]) {
		for (i = 0; i < FIELD_HEIGHT-2 && players[j+1][i]; i++)
		    mvaddch(y+i+2, x-1, players[j+1][i]);
		if (teams[j+1] != '\0') {
		    mvaddstr(y, x+FIELD_WIDTH+2, "T");
		    for (i = 0; i < FIELD_HEIGHT-2 && teams[j+1][i]; i++)
			mvaddch(y+i+2, x+FIELD_WIDTH+2, teams[j+1][i]);
		}
	    }
	    draw_other_field(j+2);
	} else {
	    sprintf(buf, "%d", j+1);
	    mvaddstr(y, x-1, buf);
	    if (players[j]) {
		for (i = 0; i < FIELD_HEIGHT-2 && players[j][i]; i++)
		    mvaddch(y+i+2, x-1, players[j][i]);
		if (teams[j] != '\0') {
		    mvaddstr(y, x+FIELD_WIDTH+2, "T");
		    for (i = 0; i < FIELD_HEIGHT-2 && teams[j][i]; i++)
			mvaddch(y+i+2, x+FIELD_WIDTH+2, teams[j][i]);
		}
	    }
	    draw_other_field(j+1);
	}
    }

    if (wide_screen) {
	x = alt_status_coord[0];
	y = alt_status_coord[1];
	mvaddstr(y, x, "Lines:");
	mvaddstr(y+1, x, "Level:");
	x = alt_next_coord[0];
	y = alt_next_coord[1];
	mvaddstr(y-2, x-1, "Next piece:");
	move(y-1, x-1);
	addch(MY_ULCORNER);
	hline(MY_HLINE, 8);
	mvaddch(y-1, x+8, MY_URCORNER);
	move(y, x-1);
	vline(MY_VLINE, 8);
	move(y, x+8);
	vline(MY_VLINE, 8);
	move(y+8, x-1);
	addch(MY_LLCORNER);
	hline(MY_HLINE, 8);
	mvaddch(y+8, x+8, MY_LRCORNER);
    } else {
	x = status_coord[0];
	y = status_coord[1];
	mvaddstr(y-1, x, "Next piece:");
	mvaddstr(y, x, "Lines:");
	mvaddstr(y+1, x, "Level:");
    }
    if (playing_game)
	draw_status();

    attdefbuf.x = wide_screen ? alt_attdef_coord[0] : attdef_coord[0];
    attdefbuf.y = wide_screen ? alt_attdef_coord[1] : attdef_coord[1];
    attdefbuf.width = (other_coord[3][0]-1) - attdefbuf.x;
    attdefbuf.height = (attdefbot+1) - attdefbuf.y;
    open_textwin(&attdefbuf);

    if (gmsg_inputwin) {
	delwin(gmsg_inputwin);
	gmsg_inputwin = NULL;
	draw_gmsg_input(NULL, -1);
    }

    (void)curs_set(0);
    screen_refresh();
    field_redraw = 0;
}

/*************************************************************************/

/* Display the player's own field. */

static void draw_own_field(void)
{
    int x, y, x0, y0;
    Field *f = &fields[my_playernum-1];
    int shadow[4] = { -1, -1, -1, -1 };

    if (dispmode != MODE_FIELDS)
	return;

    /* XXX: Code duplication with tetris.c:draw_piece(). --pasky */
    if (playing_game && cast_shadow) {
	int y = current_y - piecedata[current_piece][current_rotation].hot_y;
	char *shape = (char *) piecedata[current_piece][current_rotation].shape;
	int i, j;

	for (j = 0; j < 4; j++) {
	    if (y+j < 0) {
		shape += 4;
		continue;
	    }
	    for (i = 0; i < 4; i++) {
		if (*shape++)
		    shadow[i] = y + j;
	    }
	}
    }

    x0 = own_coord[0]+1;
    y0 = own_coord[1];
    for (y = 0; y < 22; y++) {
	for (x = 0; x < 12; x++) {
	    int c = tile_chars[(int) (*f)[y][x]];

	    if (playing_game && cast_shadow) {
		PieceData *piece = &piecedata[current_piece][current_rotation];
		int piece_x = current_x - piece->hot_x;

		if (x >= piece_x && x <= piece_x + 3
			&& shadow[(x - piece_x)] >= 0
			&& shadow[(x - piece_x)] < y
			&& ((c & 0x7f) == ' ')) {
		    c = (c & (~0x7f)) | '.'
			| getcolor(COLOR_BLACK, COLOR_BLACK) | A_BOLD;
		}
	    }

	    mvaddch(y0+y*2, x0+x*2, c);
	    addch(c);
	    mvaddch(y0+y*2+1, x0+x*2, c);
	    addch(c);
	}
    }
    if (gmsg_inputwin) {
	delwin(gmsg_inputwin);
	gmsg_inputwin = NULL;
	draw_gmsg_input(NULL, -1);
    }
    if (!field_redraw) {
	(void)curs_set(0);
	screen_refresh();
    }
}

/*************************************************************************/

/* Display another player's field. */

static void draw_other_field(int player)
{
    int x, y, x0, y0;
    Field *f;

    if (dispmode != MODE_FIELDS)
	return;
    f = &fields[player-1];
    if (player > my_playernum)
	player--;
    player--;
    x0 = other_coord[player][0]+1;
    y0 = other_coord[player][1];
    for (y = 0; y < 22; y++) {
	move(y0+y, x0);
	for (x = 0; x < 12; x++) {
	    addch(tile_chars[(int) (*f)[y][x]]);
	}
    }
    if (gmsg_inputwin) {
	delwin(gmsg_inputwin);
	gmsg_inputwin = NULL;
	draw_gmsg_input(NULL, -1);
    }
    if (!field_redraw) {
	(void)curs_set(0);
	screen_refresh();
    }
}

/*************************************************************************/

/* Display the current game status (level, lines, next piece). */

static void draw_status(void)
{
    int x, y, i, j;
    char buf[32], shape[4][4];

    x = wide_screen ? alt_status_coord[0] : status_coord[0];
    y = wide_screen ? alt_status_coord[1] : status_coord[1];
    sprintf(buf, "%d", lines>99999 ? 99999 : lines);
    mvaddstr(y, x+7, buf);
    sprintf(buf, "%d", levels[my_playernum]);
    mvaddstr(y+1, x+7, buf);
    x = wide_screen ? alt_next_coord[0] : next_coord[0];
    y = wide_screen ? alt_next_coord[1] : next_coord[1];
    if (get_shape(next_piece, 0, shape) == 0) {
	for (j = 0; j < 4; j++) {
	    if (!wide_screen)
		move(y+j, x);
	    for (i = 0; i < 4; i++) {
		if (wide_screen) {
		    move(y+j*2, x+i*2);
		    addch(tile_chars[(int) shape[j][i]]);
		    addch(tile_chars[(int) shape[j][i]]);
		    move(y+j*2+1, x+i*2);
		    addch(tile_chars[(int) shape[j][i]]);
		    addch(tile_chars[(int) shape[j][i]]);
		} else
		    addch(tile_chars[(int) shape[j][i]]);
	    }
	}
    }
}

/*************************************************************************/

/* Display the special inventory and description of the current special. */

static const char *descs[] = {
    "                    ",
    "Add Line            ",
    "Clear Line          ",
    "Nuke Field          ",
    "Clear Random Blocks ",
    "Switch Fields       ",
    "Clear Special Blocks",
    "Block Gravity       ",
    "Blockquake          ",
    "Block Bomb          "
};

static void draw_specials(void)
{
    int x, y, i;

    if (dispmode != MODE_FIELDS)
	return;
    x = own_coord[0];
    y = own_coord[1]+45;
    mvaddstr(y, x, descs[specials[0]+1]);
    move(y+1, x+10);
    i = 0;
    while (i < special_capacity && specials[i] >= 0 && x < attdef_coord[0]-1) {
	addch(tile_chars[specials[i]+6]);
	i++;
	x++;
    }
    while (x < attdef_coord[0]-1) {
	addch(tile_chars[0]);
	x++;
    }
    if (!field_redraw) {
	(void)curs_set(0);
	screen_refresh();
    }
}

/*************************************************************************/

/* Display an attack/defense message. */

static const char *msgs[][2] = {
    { "cs1", "1 Line Added to All" },
    { "cs2", "2 Lines Added to All" },
    { "cs4", "4 Lines Added to All" },
    { "a",   "Add Line" },
    { "c",   "Clear Line" },
    { "n",   "Nuke Field" },
    { "r",   "Clear Random Blocks" },
    { "s",   "Switch Fields" },
    { "b",   "Clear Special Blocks" },
    { "g",   "Block Gravity" },
    { "q",   "Blockquake" },
    { "o",   "Block Bomb" },
    { NULL }
};

static void draw_attdef(const char *type, int from, int to)
{
    int i, width;
    char buf[512];

    width = other_coord[4][0] - attdef_coord[0] - 1;
    for (i = 0; msgs[i][0]; i++) {
	if (strcmp(type, msgs[i][0]) == 0)
	    break;
    }
    if (!msgs[i][0])
	return;
    strcpy(buf, msgs[i][1]);
    if (to != 0)
	sprintf(buf+strlen(buf), " on %s", players[to-1]);
    if (from == 0)
	sprintf(buf+strlen(buf), " by Server");
    else
	sprintf(buf+strlen(buf), " by %s", players[from-1]);
    draw_text(BUFFER_ATTDEF, buf);
}

/*************************************************************************/

/* Display the in-game text window. */

static void draw_gmsg_input(const char *s, int pos)
{
    static int start = 0;	/* Start of displayed part of input line */
    static const char *last_s;
    static int last_pos;

    if (s)
	last_s = s;
    else
	s = last_s;
    if (pos >= 0)
	last_pos = pos;
    else
	pos = last_pos;

    attrset(getcolor(COLOR_WHITE,COLOR_BLACK));

    if (!gmsg_inputwin) {
	gmsg_inputpos = scrheight/2 - 1;
	gmsg_inputheight = 3;
	gmsg_inputwin =
		subwin(stdscr, gmsg_inputheight, scrwidth, gmsg_inputpos, 0);
	werase(gmsg_inputwin);
	leaveok(gmsg_inputwin, FALSE);
	leaveok(stdscr, FALSE);
	mvwaddstr(gmsg_inputwin, 1, 0, "Text>");
    }

    if (strlen(s) < scrwidth-7) {
	start = 0;
	mvwaddstr(gmsg_inputwin, 1, 6, s);
	wmove(gmsg_inputwin, 1, 6+strlen(s));
	move(gmsg_inputpos+1, 6+strlen(s));
	wclrtoeol(gmsg_inputwin);
	wmove(gmsg_inputwin, 1, 6+pos);
	move(gmsg_inputpos+1, 6+pos);
    } else {
	if (pos < start+8) {
	    start = pos-8;
	    if (start < 0)
		start = 0;
	} else if (pos > start + scrwidth-15) {
	    start = pos - (scrwidth-15);
	    if (start > strlen(s) - (scrwidth-7))
		start = strlen(s) - (scrwidth-7);
	}
	mvwaddnstr(gmsg_inputwin, 1, 6, s+start, scrwidth-6);
	wmove(gmsg_inputwin, 1, 6 + (pos-start));
	move(gmsg_inputpos+1, 6 + (pos-start));
    }
    (void)curs_set(1);
    screen_refresh();
}

/*************************************************************************/

/* Clear the in-game text window. */

static void clear_gmsg_input(void)
{
    if (gmsg_inputwin) {
	delwin(gmsg_inputwin);
	gmsg_inputwin = NULL;
	leaveok(stdscr, TRUE);
	touchline(stdscr, gmsg_inputpos, gmsg_inputheight);
	setup_fields();
        (void)curs_set(0);
	screen_refresh();
    }
}

/*************************************************************************/
/*************************** Partyline display ***************************/
/*************************************************************************/

static void setup_partyline(void)
{
    close_textwin(&gmsgbuf);
    close_textwin(&attdefbuf);
    clear();

    attrset(getcolor(COLOR_WHITE,COLOR_BLACK));

    plinebuf.x = plinebuf.y = 0;
    plinebuf.width = scrwidth;
    plinebuf.height = scrheight-4;
    open_textwin(&plinebuf);

    move(scrheight-4, 0);
    hline(MY_HLINE, scrwidth);
    move(scrheight-3, 0);
    addstr("> ");

    move(scrheight-2, 0);
    hline(MY_HLINE2, scrwidth);
    attrset(MY_BOLD);
    move(scrheight-1, 0);
    addstr("F1=Show Fields  F2=Partyline  F3=Winlist");
    move(scrheight-1, scrwidth-8);
    addstr("F10=Quit");
    attrset(A_NORMAL);

    move(scrheight-3, 2);
    leaveok(stdscr, FALSE);
    (void)curs_set(1);
    screen_refresh();
}

/*************************************************************************/

static void draw_partyline_input(const char *s, int pos)
{
    static int start = 0;	/* Start of displayed part of input line */

    attrset(getcolor(COLOR_WHITE,COLOR_BLACK));
    if (strlen(s) < scrwidth-3) {
	start = 0;
	mvaddstr(scrheight-3, 2, s);
	move(scrheight-3, 2+strlen(s));
	clrtoeol();
	move(scrheight-3, 2+pos);
    } else {
	if (pos < start+8) {
	    start = pos-8;
	    if (start < 0)
		start = 0;
	} else if (pos > start + scrwidth-11) {
	    start = pos - (scrwidth-11);
	    if (start > strlen(s) - (scrwidth-3))
		start = strlen(s) - (scrwidth-3);
	}
	mvaddnstr(scrheight-3, 2, s+start, scrwidth-2);
	move(scrheight-3, 2 + (pos-start));
    }
    screen_refresh();
}

/*************************************************************************/
/**************************** Winlist display ****************************/
/*************************************************************************/

static void setup_winlist(void)
{
    int i, x;
    char buf[32];

    leaveok(stdscr, TRUE);
    close_textwin(&plinebuf);
    clear();
    attrset(getcolor(COLOR_WHITE,COLOR_BLACK));

    for (i = 0; i < MAXWINLIST && *winlist[i].name; i++) {
	x = scrwidth/2 - strlen(winlist[i].name);
	if (x < 0)
	    x = 0;
	if (winlist[i].team) {
	    if (x < 4)
		x = 4;
	    mvaddstr(i*2, x-4, "<T>");
	}
	mvaddstr(i*2, x, winlist[i].name);
	snprintf(buf, sizeof(buf), "%4d", winlist[i].points);
	if (winlist[i].games) {
	    int avg100 = winlist[i].points*100 / winlist[i].games;
	    snprintf(buf+strlen(buf), sizeof(buf)-strlen(buf),
			"   %d.%02d",avg100/100, avg100%100);
	}
	x += strlen(winlist[i].name) + 2;
	if (x > scrwidth - strlen(buf))
	    x = scrwidth - strlen(buf);
	mvaddstr(i*2, x, buf);
    }

    move(scrheight-2, 0);
    hline(MY_HLINE2, scrwidth);
    attrset(MY_BOLD);
    move(scrheight-1, 0);
    addstr("F1=Show Fields  F2=Partyline  F3=Winlist");
    move(scrheight-1, scrwidth-8);
    addstr("F10=Quit");
    attrset(A_NORMAL);

    (void)curs_set(0);
    screen_refresh();
}

/*************************************************************************/
/************************** Interface declaration ************************/
/*************************************************************************/

Interface tty_interface = {

    wait_for_input,

    screen_setup,
    screen_refresh,
    screen_redraw,

    draw_text,
    clear_text,

    setup_fields,
    draw_own_field,
    draw_other_field,
    draw_status,
    draw_specials,
    draw_attdef,
    draw_gmsg_input,
    clear_gmsg_input,

    setup_partyline,
    draw_partyline_input,

    setup_winlist
};

/*************************************************************************/
