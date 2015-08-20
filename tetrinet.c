/* Tetrinet for Linux, by Andrew Church <achurch@achurch.org>
 * This program is public domain.
 *
 * Tetrinet main program.
 */

/*************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include "tetrinet.h"
#include "io.h"
#include "server.h"
#include "sockets.h"
#include "tetris.h"
#include "version.h"

/*************************************************************************/

int fancy = 0;		/* Fancy TTY graphics? */
int log = 0;		/* Log network traffic to file? */
char *logname;		/* Log filename */
int windows_mode = 0;	/* Try to be just like the Windows version? */
int noslide = 0;	/* Disallow piece sliding? */
int tetrifast = 0;	/* TetriFast mode? */
int cast_shadow = 1;	/* Make pieces cast shadow? */

int my_playernum = -1;	/* What player number are we? */
char *my_nick;		/* And what is our nick? */
WinInfo winlist[MAXWINLIST];  /* Winners' list from server */
int server_sock;	/* Socket for server communication */
int dispmode;		/* Current display mode */
char *players[6];	/* Player names (NULL for no such player) */
char *teams[6];		/* Team names (NULL for not on a team) */
int playing_game;	/* Are we currently playing a game? */
int not_playing_game;	/* Are we currently watching people play a game? */
int game_paused;	/* Is the game currently paused? */

Interface *io;		/* Input/output routines */

/*************************************************************************/
/*************************************************************************/

#ifndef SERVER_ONLY

/*************************************************************************/

/* Output message to a message buffer, possibly decoding the text attributes
 * tetrinet code. */

void msg_text(int bufnum, const unsigned char *s)
{
    /* Stolen from gtetrinet: (leading space <=> undefined) */
    static enum tattr map[32] = {
	 0, /* N/A */
	 TATTR_CBLACK,
	TATTR_BOLD,
	TATTR_CCYAN | TATTR_CXBRIGHT,
	TATTR_CBLACK,
	TATTR_CBLUE | TATTR_CXBRIGHT,
	TATTR_CGREY,
	 TATTR_CBLACK,
	TATTR_CMAGENTA,
	 TATTR_CBLACK,
	 TATTR_CBLACK,
	TATTR_CBLACK | TATTR_CXBRIGHT,
	TATTR_CGREEN,
	 TATTR_CBLACK,
	TATTR_CGREEN | TATTR_CXBRIGHT,
	TATTR_CGREY,
	TATTR_CRED,
	TATTR_CBLUE,
	TATTR_CBROWN,
	TATTR_CMAGENTA | TATTR_CXBRIGHT,
	TATTR_CRED | TATTR_CXBRIGHT,
	TATTR_CGREY,
	TATTR_ITALIC,
	TATTR_CCYAN,
	TATTR_CGREY | TATTR_CXBRIGHT,
	TATTR_CBROWN | TATTR_CXBRIGHT,
	 TATTR_CBLACK,
	 TATTR_CBLACK,
	 TATTR_CBLACK,
	 TATTR_CBLACK,
	 TATTR_CBLACK,
	TATTR_UNDERLINE,
    };
    unsigned char tb[1024], *t;

    for (t = tb; *s && t - tb < 1024; t++, s++) {
	if (*s == 0xFF) {
	    *t = TATTR_RESET + 1;
	} else if (*s < 32) {
	    *t = map[(int) *s] + 1;
	} else {
	    *t = *s;
	}
    }
    if (t - tb >= 1024) t = &tb[1023];
    *t = 0;

    io->draw_text(bufnum, tb);
}


/* Parse a line from the server.  Destroys the buffer it's given as a side
 * effect.
 */

void parse(char *buf)
{
    char *cmd, *s, *t;

    cmd = strtok(buf, " ");

    if (!cmd) {
	return;

    } else if (strcmp(cmd, "noconnecting") == 0) {
	s = strtok(NULL, "");
	if (!s)
	    s = "Unknown";
	/* XXX not to stderr, please! -- we need to stay running w/o server */
	fprintf(stderr, "Server error: %s\n", s);
	exit(1);

    } else if (strcmp(cmd, "winlist") == 0) {
	int i = 0;

	while (i < MAXWINLIST && (s = strtok(NULL, " "))) {
	    t = strchr(s, ';');
	    if (!t)
		break;
	    *t++ = 0;
	    if (*s == 't')
		winlist[i].team = 1;
	    else
		winlist[i].team = 0;
	    s++;
	    strncpy(winlist[i].name, s, sizeof(winlist[i].name)-1);
	    winlist[i].name[sizeof(winlist[i].name)] = 0;
	    winlist[i].points = atoi(t);
	    if ((t = strchr(t, ';')) != NULL)
		winlist[i].games = atoi(t+1);
	    i++;
	}
	if (i < MAXWINLIST)
	    winlist[i].name[0] = 0;
	if (dispmode == MODE_WINLIST)
	    io->setup_winlist();

    } else if (strcmp(cmd, tetrifast ? ")#)(!@(*3" : "playernum") == 0) {
	if ((s = strtok(NULL, " ")))
	    my_playernum = atoi(s);
	/* Note: players[my_playernum-1] is set in init() */
	/* But that doesn't work when joining other channel. */
	players[my_playernum-1] = strdup(my_nick);

    } else if (strcmp(cmd, "playerjoin") == 0) {
	int player;
	char buf[1024];

	s = strtok(NULL, " ");
	t = strtok(NULL, "");
	if (!s || !t)
	    return;
	player = atoi(s)-1;
	if (player < 0 || player > 5)
	    return;
	players[player] = strdup(t);
	if (teams[player]) {
	    free(teams[player]);
	    teams[player] = NULL;
	}
	snprintf(buf, sizeof(buf), "*** %s is Now Playing", t);
	msg_text(BUFFER_PLINE, buf);
	if (dispmode == MODE_FIELDS)
	    io->setup_fields();

    } else if (strcmp(cmd, "playerleave") == 0) {
	int player;
	char buf[1024];

	s = strtok(NULL, " ");
	if (!s)
	    return;
	player = atoi(s)-1;
	if (player < 0 || player > 5 || !players[player])
	    return;
	snprintf(buf, sizeof(buf), "*** %s has Left", players[player]);
	msg_text(BUFFER_PLINE, buf);
	free(players[player]);
	players[player] = NULL;
	if (dispmode == MODE_FIELDS)
	    io->setup_fields();

    } else if (strcmp(cmd, "team") == 0) {
	int player;
	char buf[1024];

	s = strtok(NULL, " ");
	t = strtok(NULL, "");
	if (!s)
	    return;
	player = atoi(s)-1;
	if (player < 0 || player > 5 || !players[player])
	    return;
	if (teams[player])
	    free(teams[player]);
	if (t)
	    teams[player] = strdup(t);
	else
	    teams[player] = NULL;
	if (t)
	    snprintf(buf, sizeof(buf), "*** %s is Now on Team %s", players[player], t);
	else
	    snprintf(buf, sizeof(buf), "*** %s is Now Alone", players[player]);
	msg_text(BUFFER_PLINE, buf);

    } else if (strcmp(cmd, "pline") == 0) {
	int playernum;
	char buf[1024], *name;

	s = strtok(NULL, " ");
	t = strtok(NULL, "");
	if (!s)
	    return;
	if (!t)
	    t = "";
	playernum = atoi(s)-1;
	if (playernum == -1) {
	    name = "Server";
	} else {
	    if (playernum < 0 || playernum > 5 || !players[playernum])
		return;
	    name = players[playernum];
	}
	snprintf(buf, sizeof(buf), "<%s> %s", name, t);
	msg_text(BUFFER_PLINE, buf);

    } else if (strcmp(cmd, "plineact") == 0) {
	int playernum;
	char buf[1024], *name;

	s = strtok(NULL, " ");
	t = strtok(NULL, "");
	if (!s)
	    return;
	if (!t)
	    t = "";
	playernum = atoi(s)-1;
	if (playernum == -1) {
	    name = "Server";
	} else {
	    if (playernum < 0 || playernum > 5 || !players[playernum])
		return;
	    name = players[playernum];
	}
	snprintf(buf, sizeof(buf), "* %s %s", name, t);
	msg_text(BUFFER_PLINE, buf);

    } else if (strcmp(cmd, tetrifast ? "*******" : "newgame") == 0) {
	int i;

	if ((s = strtok(NULL, " ")))
	    /* stack height */;
	if ((s = strtok(NULL, " ")))
	    initial_level = atoi(s);
	if ((s = strtok(NULL, " ")))
	    lines_per_level = atoi(s);
	if ((s = strtok(NULL, " ")))
	    level_inc = atoi(s);
	if ((s = strtok(NULL, " ")))
	    special_lines = atoi(s);
	if ((s = strtok(NULL, " ")))
	    special_count = atoi(s);
	if ((s = strtok(NULL, " "))) {
	    special_capacity = atoi(s);
	    if (special_capacity > MAX_SPECIALS)
		special_capacity = MAX_SPECIALS;
	}
	if ((s = strtok(NULL, " "))) {
	    memset(piecefreq, 0, sizeof(piecefreq));
	    while (*s) {
		i = *s - '1';
		if (i >= 0 && i < 7)
		    piecefreq[i]++;
		s++;
	    }
	}
	if ((s = strtok(NULL, " "))) {
	    memset(specialfreq, 0, sizeof(specialfreq));
	    while (*s) {
		i = *s - '1';
		if (i >= 0 && i < 9)
		    specialfreq[i]++;
		s++;
	    }
	}
	if ((s = strtok(NULL, " ")))
	    level_average = atoi(s);
	if ((s = strtok(NULL, " ")))
	    old_mode = atoi(s);
	lines = 0;
	for (i = 0; i < 6; i++)
	    levels[i] = initial_level;
	memset(&fields[my_playernum-1], 0, sizeof(Field));
	specials[0] = -1;
	io->clear_text(BUFFER_GMSG);
	io->clear_text(BUFFER_ATTDEF);
	new_game();
	playing_game = 1;
	game_paused = 0;
	msg_text(BUFFER_PLINE, "*** The Game Has Started");
	if (dispmode != MODE_FIELDS) {
	    dispmode = MODE_FIELDS;
	    io->setup_fields();
	}

    } else if (strcmp(cmd, "ingame") == 0) {
	/* Sent when a player connects in the middle of a game */
	int x, y;
	char buf[1024], *s;

	s = buf + sprintf(buf, "f %d ", my_playernum);
	for (y = 0; y < FIELD_HEIGHT; y++) {
	    for (x = 0; x < FIELD_WIDTH; x++) {
		fields[my_playernum-1][y][x] = rand()%5 + 1;
		*s++ = '0' + fields[my_playernum-1][y][x];
	    }
	}
	*s = 0;
	sputs(buf, server_sock);
	playing_game = 0;
	not_playing_game = 1;

    } else if (strcmp(cmd, "pause") == 0) {
	if ((s = strtok(NULL, " ")))
	    game_paused = atoi(s);
	if (game_paused) {
	    msg_text(BUFFER_PLINE, "*** The Game Has Been Paused");
	    msg_text(BUFFER_GMSG, "*** The Game Has Been Paused");
	} else {
	    msg_text(BUFFER_PLINE, "*** The Game Has Been Unpaused");
	    msg_text(BUFFER_GMSG, "*** The Game Has Been Unpaused");
	}

    } else if (strcmp(cmd, "endgame") == 0) {
	playing_game = 0;
	not_playing_game = 0;
	memset(fields, 0, sizeof(fields));
	specials[0] = -1;
	io->clear_text(BUFFER_ATTDEF);
	msg_text(BUFFER_PLINE, "*** The Game Has Ended");
	if (dispmode == MODE_FIELDS) {
	    int i;
	    io->draw_own_field();
	    for (i = 1; i <= 6; i++) {
		if (i != my_playernum)
		    io->draw_other_field(i);
	    }
	}
	if (dispmode != MODE_PARTYLINE) {
	    dispmode = MODE_PARTYLINE;
	    io->setup_partyline();
	}

    } else if (strcmp(cmd, "playerwon") == 0) {
	/* Syntax: playerwon # -- sent when all but one player lose */

    } else if (strcmp(cmd, "playerlost") == 0) {
	/* Syntax: playerlost # -- sent after playerleave on disconnect
	 *     during a game, or when a player loses (sent by the losing
	 *     player and from the server to all other players */

    } else if (strcmp(cmd, "f") == 0) {   /* field */
	int player, x, y, tile;

	/* This looks confusing, but what it means is, ignore this message
	 * if a game isn't going on. */
	if (!playing_game && !not_playing_game)
	    return;
	if (!(s = strtok(NULL, " ")))
	    return;
	player = atoi(s);
	player--;
	if (!(s = strtok(NULL, "")))
	    return;
	if (*s >= '0') {
	    /* Set field directly */
	    char *ptr = (char *) fields[player];
	    while (*s) {
		if (*s <= '5')
		    *ptr++ = (*s++) - '0';
		else switch (*s++) {
		    case 'a': *ptr++ = 6 + SPECIAL_A; break;
		    case 'b': *ptr++ = 6 + SPECIAL_B; break;
		    case 'c': *ptr++ = 6 + SPECIAL_C; break;
		    case 'g': *ptr++ = 6 + SPECIAL_G; break;
		    case 'n': *ptr++ = 6 + SPECIAL_N; break;
		    case 'o': *ptr++ = 6 + SPECIAL_O; break;
		    case 'q': *ptr++ = 6 + SPECIAL_Q; break;
		    case 'r': *ptr++ = 6 + SPECIAL_R; break;
		    case 's': *ptr++ = 6 + SPECIAL_S; break;
		}
	    }
	} else {
	    /* Set specific locations on field */
	    tile = 0;
	    while (*s) {
		if (*s < '0') {
		    tile = *s - '!';
		} else {
		    x = *s - '3';
		    y = (*++s) - '3';
		    fields[player][y][x] = tile;
		}
		s++;
	    }
	}
	if (player == my_playernum-1)
	    io->draw_own_field();
	else
	    io->draw_other_field(player+1);
    } else if (strcmp(cmd, "lvl") == 0) {
	int player;

	if (!(s = strtok(NULL, " ")))
	    return;
	player = atoi(s)-1;
	if (!(s = strtok(NULL, "")))
	    return;
	levels[player] = atoi(s);

    } else if (strcmp(cmd, "sb") == 0) {
	int from, to;
	char *type;

	if (!(s = strtok(NULL, " ")))
	    return;
	to = atoi(s);
	if (!(type = strtok(NULL, " ")))
	    return;
	if (!(s = strtok(NULL, " ")))
	    return;
	from = atoi(s);
	do_special(type, from, to);

    } else if (strcmp(cmd, "gmsg") == 0) {
	if (!(s = strtok(NULL, "")))
	    return;
	msg_text(BUFFER_GMSG, s);

    }
}

/*************************************************************************/
/*************************************************************************/

static char partyline_buffer[512];
static int partyline_pos = 0;

#define curpos	(partyline_buffer+partyline_pos)

/*************************************************************************/

/* Add a character to the partyline buffer. */

void partyline_input(int c)
{
    if (partyline_pos < sizeof(partyline_buffer) - 1) {
	memmove(curpos+1, curpos, strlen(curpos)+1);
	partyline_buffer[partyline_pos++] = c;
	io->draw_partyline_input(partyline_buffer, partyline_pos);
    }
}

/*************************************************************************/

/* Delete the current character from the partyline buffer. */

void partyline_delete(void)
{
    if (partyline_buffer[partyline_pos]) {
	memmove(curpos, curpos+1, strlen(curpos)-1+1);
	io->draw_partyline_input(partyline_buffer, partyline_pos);
    }
}

/*************************************************************************/

/* Backspace a character from the partyline buffer. */

void partyline_backspace(void)
{
    if (partyline_pos > 0) {
	partyline_pos--;
	partyline_delete();
    }
}

/*************************************************************************/

/* Kill the entire partyline input buffer. */

void partyline_kill(void)
{
    partyline_pos = 0;
    *partyline_buffer = 0;
    io->draw_partyline_input(partyline_buffer, partyline_pos);
}

/*************************************************************************/

/* Move around the input buffer.  Sign indicates direction; absolute value
 * of 1 means one character, 2 means the whole line.
 */

void partyline_move(int how)
{
    if (how == -2) {
	partyline_pos = 0;
	io->draw_partyline_input(partyline_buffer, partyline_pos);
    } else if (how == -1 && partyline_pos > 0) {
	partyline_pos--;
	io->draw_partyline_input(partyline_buffer, partyline_pos);
    } else if (how == 1 && partyline_buffer[partyline_pos]) {
	partyline_pos++;
	io->draw_partyline_input(partyline_buffer, partyline_pos);
    } else if (how == 2) {
	partyline_pos = strlen(partyline_buffer);
	io->draw_partyline_input(partyline_buffer, partyline_pos);
    }
}

/*************************************************************************/

/* Send the input line to the server. */

void partyline_enter(void)
{
    char buf[1024];

    if (*partyline_buffer) {
	if (strncasecmp(partyline_buffer, "/me ", 4) == 0) {
	    sockprintf(server_sock, "plineact %d %s", my_playernum, partyline_buffer+4);
	    snprintf(buf, sizeof(buf), "* %s %s", players[my_playernum-1], partyline_buffer+4);
	    msg_text(BUFFER_PLINE, buf);
	} else if (strcasecmp(partyline_buffer, "/start") == 0) {
	    sockprintf(server_sock, "startgame 1 %d", my_playernum);
	} else if (strcasecmp(partyline_buffer, "/end") == 0) {
	    sockprintf(server_sock, "startgame 0 %d", my_playernum);
	} else if (strcasecmp(partyline_buffer, "/pause") == 0) {
	    sockprintf(server_sock, "pause 1 %d", my_playernum);
	} else if (strcasecmp(partyline_buffer, "/unpause") == 0) {
	    sockprintf(server_sock, "pause 0 %d", my_playernum);
	} else if (strncasecmp(partyline_buffer, "/team", 5) == 0) {
	    if (strlen(partyline_buffer) == 5)
		strcpy(partyline_buffer+5, " ");  /* make it "/team " */
	    sockprintf(server_sock, "team %d %s", my_playernum, partyline_buffer+6);
	    if (partyline_buffer[6]) {
		if (teams[my_playernum-1])
		    free(teams[my_playernum-1]);
		teams[my_playernum-1] = strdup(partyline_buffer+6);
		snprintf(buf, sizeof(buf), "*** %s is Now on Team %s", players[my_playernum-1], partyline_buffer+6);
		msg_text(BUFFER_PLINE, buf);
	    } else {
		if (teams[my_playernum-1])
		    free(teams[my_playernum-1]);
		teams[my_playernum-1] = NULL;
		snprintf(buf, sizeof(buf), "*** %s is Now Alone", players[my_playernum-1]);
		msg_text(BUFFER_PLINE, buf);
	    }
	} else {
	    sockprintf(server_sock, "pline %d %s", my_playernum, partyline_buffer);
	    if (*partyline_buffer != '/'
		|| partyline_buffer[1] == 0 || partyline_buffer[1] == ' ') {
		/* We do not show server-side commands. */
		snprintf(buf, sizeof(buf), "<%s> %s", players[my_playernum-1], partyline_buffer);
		msg_text(BUFFER_PLINE, buf);
	    }
	}
	partyline_pos = 0;
	*partyline_buffer = 0;
	io->draw_partyline_input(partyline_buffer, partyline_pos);
    }
}

#undef curpos

/*************************************************************************/
/*************************************************************************/

void help()
{
    fprintf(stderr,
"Tetrinet " VERSION " - Text-mode tetrinet client\n"
"\n"
"Usage: tetrinet [OPTION]... NICK SERVER\n"
"\n"
"Options (see README for details):\n"
"  -fancy       Use \"fancy\" TTY graphics.\n"
"  -fast        Connect to the server in the tetrifast mode.\n"
"  -log <file>  Log network traffic to the given file.\n"
"  -noshadow    Do not make the pieces cast shadow.\n"
"  -noslide     Do not allow pieces to \"slide\" after being dropped\n"
"               with the spacebar.\n"
"  -server      Start the server instead of the client.\n"
"  -shadow      Make the pieces cast shadow. Can speed up gameplay\n"
"               considerably, but it can be considered as cheating by\n"
"               some people since some other tetrinet clients lack this.\n"
"  -slide       Opposite of -noslide; allows pieces to \"slide\" after\n"
"               being dropped.  If both -slide and -noslide are given,\n"
"               -slide takes precedence.\n"
"  -windows     Behave as much like the Windows version of Tetrinet as\n"
"               possible. Implies -noslide and -noshadow.\n"
	   );
}

int init(int ac, char **av)
{
    int i;
    char *nick = NULL, *server = NULL;
    char buf[1024];
    char nickmsg[1024];
    unsigned char ip[4];
    char iphashbuf[32];
    int len;
#ifdef BUILTIN_SERVER
    int start_server = 0;   /* Start the server? (-server) */
#endif
    int slide = 0;	    /* Do we definitely want to slide? (-slide) */


    /* If there's a DISPLAY variable set in the environment, default to
     * Xwindows I/O, else default to terminal I/O. */
    /* if (getenv("DISPLAY"))
	io = &xwin_interface;
    else
	io = &tty_interface; */
    io=&tty_interface;  /* because Xwin isn't done yet */

    srand(time(NULL));
    init_shapes();

    for (i = 1; i < ac; i++) {
	if (*av[i] == '-') {
#ifdef BUILTIN_SERVER
	    if (strcmp(av[i], "-server") == 0) {
		start_server = 1;
	    } else
#endif
	    if (strcmp(av[i], "-fancy") == 0) {
		fancy = 1;
	    } else if (strcmp(av[i], "-log") == 0) {
		log = 1;
		i++;
		if (i >= ac) {
		    fprintf(stderr, "Option -log requires an argument\n");
		    return 1;
		}
		logname = av[i];
	    } else if (strcmp(av[i], "-noslide") == 0) {
		noslide = 1;
	    } else if (strcmp(av[i], "-noshadow") == 0) {
		cast_shadow = 0;
	    } else if (strcmp(av[i], "-shadow") == 0) {
		cast_shadow = 1;
	    } else if (strcmp(av[i], "-slide") == 0) {
		slide = 1;
	    } else if (strcmp(av[i], "-windows") == 0) {
		windows_mode = 1;
		noslide = 1;
		cast_shadow = 0;
	    } else if (strcmp(av[i], "-fast") == 0) {
		tetrifast = 1;
	    } else {
		fprintf(stderr, "Unknown option %s\n", av[i]);
	        help();
		return 1;
	    }
	} else if (!nick) {
	    my_nick = nick = av[i];
	} else if (!server) {
	    server = av[i];
	} else {
	    help();
	    return 1;
	}
    }
    if (slide)
	noslide = 0;
#ifdef BUILTIN_SERVER
    if (start_server)
	exit(server_main());
#endif
    if (!server) {
	help();
	return 1;
    }
    if (strlen(nick) > 63)  /* put a reasonable limit on nick length */
	nick[63] = 0;

    if ((server_sock = conn(server, 31457, ip)) < 0) {
	fprintf(stderr, "Couldn't connect to server %s: %s\n",
		server, strerror(errno));
	return 1;
    }
    sprintf(nickmsg, "tetri%s %s 1.13", tetrifast ? "faster" : "sstart", nick);
    sprintf(iphashbuf, "%d", ip[0]*54 + ip[1]*41 + ip[2]*29 + ip[3]*17);
    /* buf[0] does not need to be initialized for this algorithm */
    len = strlen(nickmsg);
    for (i = 0; i < len; i++)
	buf[i+1] = (((buf[i]&0xFF) + (nickmsg[i]&0xFF)) % 255) ^ iphashbuf[i % strlen(iphashbuf)];
    len++;
    for (i = 0; i < len; i++)
	sprintf(nickmsg+i*2, "%02X", buf[i] & 0xFF);
    sputs(nickmsg, server_sock);

    do {
	if (!sgets(buf, sizeof(buf), server_sock)) {
	    fprintf(stderr, "Server %s closed connection\n", server);
	    disconn(server_sock);
	    return 1;
	}
	parse(buf);
    } while (my_playernum < 0);
    sockprintf(server_sock, "team %d ", my_playernum);

    players[my_playernum-1] = strdup(nick);
    dispmode = MODE_PARTYLINE;
    io->screen_setup();
    io->setup_partyline();

    return 0;
}

/*************************************************************************/

int main(int ac, char **av)
{
    int i;

    if ((i = init(ac, av)) != 0)
	return i;

    for (;;) {
	int timeout;
	if (playing_game && !game_paused)
	    timeout = tetris_timeout();
	else
	    timeout = -1;
	i = io->wait_for_input(timeout);
	if (i == -1) {
	    char buf[1024];
	    if (sgets(buf, sizeof(buf), server_sock))
		parse(buf);
	    else {
		msg_text(BUFFER_PLINE, "*** Disconnected from Server");
		break;
	    }
	} else if (i == -2) {
	    tetris_timeout_action();
	} else if (i == 12) {  /* Ctrl-L */
	    io->screen_redraw();
	} else if (i == K_F10) {
	    break;  /* out of main loop */
	} else if (i == K_F1) {
	    if (dispmode != MODE_FIELDS) {
		dispmode = MODE_FIELDS;
		io->setup_fields();
	    }
	} else if (i == K_F2) {
	    if (dispmode != MODE_PARTYLINE) {
		dispmode = MODE_PARTYLINE;
		io->setup_partyline();
	    }
	} else if (i == K_F3) {
	    if (dispmode != MODE_WINLIST) {
		dispmode = MODE_WINLIST;
		io->setup_winlist();
	    }
	} else if (dispmode == MODE_FIELDS) {
	    tetris_input(i);
	} else if (dispmode == MODE_PARTYLINE) {
	    if (i == 8 || i == 127)   /* Backspace or Delete */
		partyline_backspace();
	    else if (i == 4)    /* Ctrl-D */
		partyline_delete();
	    else if (i == 21)   /* Ctrl-U */
		partyline_kill();
	    else if (i == '\r' || i == '\n')
		partyline_enter();
	    else if (i == K_LEFT)
		partyline_move(-1);
	    else if (i == K_RIGHT)
		partyline_move(1);
	    else if (i == 1)    /* Ctrl-A */
		partyline_move(-2);
	    else if (i == 5)    /* Ctrl-E */
		partyline_move(2);
	    else if (i >= 1 && i <= 0xFF)
		partyline_input(i);
	}
    }

    disconn(server_sock);
    return 0;
}

/*************************************************************************/

#endif	/* !SERVER_ONLY */

/*************************************************************************/
