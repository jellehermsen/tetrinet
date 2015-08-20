/* Tetrinet for Linux, by Andrew Church <achurch@achurch.org>
 * This program is public domain.
 *
 * Tetrinet server code
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <netinet/in.h>
/* Due to glibc brokenness, we can't blindly include this.  Yet another
 * reason to not use glibc. */
/* #include <netinet/protocols.h> */
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include "tetrinet.h"
#include "tetris.h"
#include "server.h"
#include "sockets.h"

/*************************************************************************/

static int linuxmode = 0;  /* 1: don't try to be compatible with Windows */
static int ipv6_only = 0;  /* 1: only use IPv6 (when available) */

static int quit = 0;

static int listen_sock = -1;
#ifdef HAVE_IPV6
static int listen_sock6 = -1;
#endif
static int player_socks[6] = {-1,-1,-1,-1,-1,-1};
static unsigned char player_ips[6][4];
static int player_modes[6];

/* Which players have already lost in the current game? */
static int player_lost[6];

/* We re-use a lot of variables from the main code */

/*************************************************************************/
/*************************************************************************/

/* Convert a 2-byte hex value to an integer. */

int xtoi(const char *buf)
{
    int val;

    if (buf[0] <= '9')
	val = (buf[0] - '0') << 4;
    else
	val = (toupper(buf[0]) - 'A' + 10) << 4;
    if (buf[1] <= '9')
	val |= buf[1] - '0';
    else
	val |= toupper(buf[1]) - 'A' + 10;
    return val;
}

/*************************************************************************/

/* Return a string containing the winlist in a format suitable for sending
 * to clients.
 */

static char *winlist_str()
{
    static char buf[1024];
    char *s;
    int i;

    s = buf;
    for (i = 0; i < MAXWINLIST && *winlist[i].name; i++) {
	s += snprintf(s, sizeof(buf)-(s-buf),
			linuxmode ? " %c%s;%d;%d" : " %c%s;%d",
			winlist[i].team ? 't' : 'p',
			winlist[i].name, winlist[i].points, winlist[i].games);
    }
    return buf;
}

/*************************************************************************/
/*************************************************************************/

/* Read the configuration file. */

void read_config(void)
{
    char buf[1024], *s, *t;
    FILE *f;
    int i;

    s = getenv("HOME");
    if (!s)
	s = "/etc";
    snprintf(buf, sizeof(buf), "%s/.tetrinet", s);
    if (!(f = fopen(buf, "r")))
	return;
    while (fgets(buf, sizeof(buf), f)) {
	s = strtok(buf, " ");
	if (!s) {
	    continue;
	} else if (strcmp(s, "linuxmode") == 0) {
	    if ((s = strtok(NULL, " ")))
		linuxmode = atoi(s);
	} else if (strcmp(s, "ipv6_only") == 0) {
	    if ((s = strtok(NULL, " ")))
		ipv6_only = atoi(s);
	} else if (strcmp(s, "averagelevels") == 0) {
	    if ((s = strtok(NULL, " ")))
		level_average = atoi(s);
	} else if (strcmp(s, "classic") == 0) {
	    if ((s = strtok(NULL, " ")))
		old_mode = atoi(s);
	} else if (strcmp(s, "initiallevel") == 0) {
	    if ((s = strtok(NULL, " ")))
		initial_level = atoi(s);
	} else if (strcmp(s, "levelinc") == 0) {
	    if ((s = strtok(NULL, " ")))
		level_inc = atoi(s);
	} else if (strcmp(s, "linesperlevel") == 0) {
	    if ((s = strtok(NULL, " ")))
		lines_per_level = atoi(s);
	} else if (strcmp(s, "pieces") == 0) {
	    i = 0;
	    while (i < 7 && (s = strtok(NULL, " ")))
		piecefreq[i++] = atoi(s);
	} else if (strcmp(s, "specialcapacity") == 0) {
	    if ((s = strtok(NULL, " ")))
		special_capacity = atoi(s);
	} else if (strcmp(s, "specialcount") == 0) {
	    if ((s = strtok(NULL, " ")))
		special_count = atoi(s);
	} else if (strcmp(s, "speciallines") == 0) {
	    if ((s = strtok(NULL, " ")))
		special_lines = atoi(s);
	} else if (strcmp(s, "specials") == 0) {
	    i = 0;
	    while (i < 9 && (s = strtok(NULL, " ")))
		specialfreq[i++] = atoi(s);
	} else if (strcmp(s, "winlist") == 0) {
	    i = 0;
	    while (i < MAXWINLIST && (s = strtok(NULL, " "))) {
		t = strchr(s, ';');
		if (!t)
		    break;
		*t++ = 0;
		strncpy(winlist[i].name, s, sizeof(winlist[i].name)-1);
		winlist[i].name[sizeof(winlist[i].name)-1] = 0;
		s = t;
		t = strchr(s, ';');
		if (!t) {
		    *winlist[i].name = 0;
		    break;
		}
		winlist[i].team = atoi(s);
		s = t+1;
		t = strchr(s, ';');
		if (!t) {
		    *winlist[i].name = 0;
		    break;
		}
		winlist[i].points = atoi(s);
		winlist[i].games = atoi(t+1);
		i++;
	    }
	}
    }
    fclose(f);
}

/*************************************************************************/

/* Re-write the configuration file. */

void write_config(void)
{
    char buf[1024], *s;
    FILE *f;
    int i;

    s = getenv("HOME");
    if (!s)
	s = "/etc";
    snprintf(buf, sizeof(buf), "%s/.tetrinet", s);
    if (!(f = fopen(buf, "w")))
	return;

    fprintf(f, "winlist");
    for (i = 0; i < MAXSAVEWINLIST && *winlist[i].name; i++) {
	fprintf(f, " %s;%d;%d;%d", winlist[i].name, winlist[i].team,
				   winlist[i].points, winlist[i].games);
    }
    fputc('\n', f);

    fprintf(f, "classic %d\n", old_mode);

    fprintf(f, "initiallevel %d\n", initial_level);
    fprintf(f, "linesperlevel %d\n", lines_per_level);
    fprintf(f, "levelinc %d\n", level_inc);
    fprintf(f, "averagelevels %d\n", level_average);

    fprintf(f, "speciallines %d\n", special_lines);
    fprintf(f, "specialcount %d\n", special_count);
    fprintf(f, "specialcapacity %d\n", special_capacity);

    fprintf(f, "pieces");
    for (i = 0; i < 7; i++)
	fprintf(f, " %d", piecefreq[i]);
    fputc('\n', f);

    fprintf(f, "specials");
    for (i = 0; i < 9; i++)
	fprintf(f, " %d", specialfreq[i]);
    fputc('\n', f);

    fprintf(f, "linuxmode %d\n", linuxmode);
    fprintf(f, "ipv6_only %d\n", ipv6_only);

    fclose(f);
}

/*************************************************************************/
/*************************************************************************/

/* Send a message to a single player. */

static void send_to(int player, const char *format, ...)
{
    va_list args;
    char buf[1024];

    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    if (player_socks[player-1] >= 0)
	sockprintf(player_socks[player-1], "%s", buf);
}

/*************************************************************************/

/* Send a message to all players. */

static void send_to_all(const char *format, ...)
{
    va_list args;
    char buf[1024];
    int i;

    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    for (i = 0; i < 6; i++) {
	if (player_socks[i] >= 0)
	    sockprintf(player_socks[i], "%s", buf);
    }
}

/*************************************************************************/

/* Send a message to all players but the given one. */

static void send_to_all_but(int player, const char *format, ...)
{
    va_list args;
    char buf[1024];
    int i;

    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    for (i = 0; i < 6; i++) {
	if (i+1 != player && player_socks[i] >= 0)
	    sockprintf(player_socks[i], "%s", buf);
    }
}

/*************************************************************************/

/* Send a message to all players but those on the same team as the given
 * player.
 */

static void send_to_all_but_team(int player, const char *format, ...)
{
    va_list args;
    char buf[1024];
    int i;
    char *team = teams[player-1];

    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    for (i = 0; i < 6; i++) {
	if (i+1 != player && player_socks[i] >= 0 &&
			(!team || !teams[i] || strcmp(teams[i], team) != 0))
	    sockprintf(player_socks[i], "%s", buf);
    }
}

/*************************************************************************/
/*************************************************************************/

/* Add points to a given player's [team's] winlist entry, or make a new one
 * if they rank.
 */

static void add_points(int player, int points)
{
    int i;

    if (!players[player-1])
	return;
    for (i = 0; i < MAXWINLIST && *winlist[i].name; i++) {
	if (!winlist[i].team && !teams[player-1]
	 && strcmp(winlist[i].name, players[player-1]) == 0)
	    break;
	if (winlist[i].team && teams[player-1]
	 && strcmp(winlist[i].name, teams[player-1]) == 0)
	    break;
    }
    if (i == MAXWINLIST) {
	for (i = 0; i < MAXWINLIST && winlist[i].points >= points; i++)
	    ;
    }
    if (i == MAXWINLIST)
	return;
    if (!*winlist[i].name) {
	if (teams[player-1]) {
	    strncpy(winlist[i].name, teams[player-1], sizeof(winlist[i].name)-1);
	    winlist[i].name[sizeof(winlist[i].name)-1] = 0;
	    winlist[i].team = 1;
	} else {
	    strncpy(winlist[i].name, players[player-1], sizeof(winlist[i].name)-1);
	    winlist[i].name[sizeof(winlist[i].name)-1] = 0;
	    winlist[i].team = 0;
	}
    }
    winlist[i].points += points;
}

/*************************************************************************/

/* Add a game to a given player's [team's] winlist entry. */

static void add_game(int player)
{
    int i;

    if (!players[player-1])
	return;
    for (i = 0; i < MAXWINLIST && *winlist[i].name; i++) {
	if (!winlist[i].team && !teams[player-1]
	 && strcmp(winlist[i].name, players[player-1]) == 0)
	    break;
	if (winlist[i].team && teams[player-1]
	 && strcmp(winlist[i].name, teams[player-1]) == 0)
	    break;
    }
    if (i == MAXWINLIST || !*winlist[i].name)
	return;
    winlist[i].games++;
}

/*************************************************************************/

/* Sort the winlist. */

static void sort_winlist()
{
    int i, j, best, bestindex;

    for (i = 0; i < MAXWINLIST && *winlist[i].name; i++) {
	best = winlist[i].points;
	bestindex = i;
	for (j = i+1; j < MAXWINLIST && *winlist[j].name; j++) {
	    if (winlist[j].points > best) {
		best = winlist[j].points;
		bestindex = j;
	    }
	}
	if (bestindex != i) {
	    WinInfo tmp;
	    memcpy(&tmp, &winlist[i], sizeof(WinInfo));
	    memcpy(&winlist[i], &winlist[bestindex], sizeof(WinInfo));
	    memcpy(&winlist[bestindex], &tmp, sizeof(WinInfo));
	}
    }
}

/*************************************************************************/

/* Take care of a player losing (which may end the game). */

static void player_loses(int player)
{
    int i, j, order, end = 1, winner = -1, second = -1, third = -1;

    if (player < 1 || player > 6 || player_socks[player-1] < 0)
	return;
    order = 0;
    for (i = 1; i <= 6; i++) {
	if (player_lost[i-1] > order)
	    order = player_lost[i-1];
    }
    player_lost[player-1] = order+1;
    for (i = 1; i <= 6; i++) {
	if (player_socks[i-1] >= 0 && !player_lost[i-1]) {
	    if (winner < 0) {
		winner = i;
	    } else if (!teams[winner-1] || !teams[i-1]
			|| strcasecmp(teams[winner-1],teams[i-1]) != 0) {
		end = 0;
		break;
	    }
	}
    }
    if (end) {
	send_to_all("endgame");
	playing_game = 0;
	/* Catch the case where no players are left (1-player game) */
	if (winner > 0) {
	    send_to_all("playerwon %d", winner);
	    add_points(winner, 3);
	    order = 0;
	    for (i = 1; i <= 6; i++) {
		if (player_lost[i-1] > order
			&& (!teams[winner-1] || !teams[i-1]
			    || strcasecmp(teams[winner-1],teams[i-1]) != 0)) {
		    order = player_lost[i-1];
		    second = i;
		}
	    }
	    if (order) {
		add_points(second, 2);
		player_lost[second-1] = 0;
	    }
	    order = 0;
	    for (i = 1; i <= 6; i++) {
		if (player_lost[i-1] > order
			&& (!teams[winner-1] || !teams[i-1]
			    || strcasecmp(teams[winner-1],teams[i-1]) != 0)
			&& (!teams[second-1] || !teams[i-1]
			    || strcasecmp(teams[second-1],teams[i-1]) != 0)) {
		    order = player_lost[i-1];
		    third = i;
		}
	    }
	    if (order)
		add_points(third, 1);
	    for (i = 1; i <= 6; i++) {
		if (teams[i-1]) {
		    for (j = 1; j < i; j++) {
			if (teams[j-1] && strcasecmp(teams[i-1],teams[j-1])==0)
			    break;
		    }
		    if (j < i)
			continue;
		}
		if (player_socks[i-1] >= 0)
		    add_game(i);
	    }
	}
	sort_winlist();
	write_config();
	send_to_all("winlist %s", winlist_str());
    }
    /* One more possibility: the only player playing left the game, which
     * means there are now no players left. */
    if (!players[0] && !players[1] && !players[2] && !players[3]
                    && !players[4] && !players[5])
	playing_game = 0;
}

/*************************************************************************/
/*************************************************************************/

/* Parse a line from a client.  Destroys the buffer it's given as a side
 * effect.  Return 0 if the command is unknown (or bad syntax), else 1.
 */

static int server_parse(int player, char *buf)
{
    char *cmd, *s, *t;
    int i, tetrifast = 0;

    cmd = strtok(buf, " ");

    if (!cmd) {
	return 1;

    } else if (strcmp(cmd, "tetrisstart") == 0) {
newplayer:
	s = strtok(NULL, " ");
	t = strtok(NULL, " ");
	if (!t)
	    return 0;
	for (i = 1; i <= 6; i++) {
	    if (players[i-1] && strcasecmp(s, players[i-1]) == 0) {
		send_to(player, "noconnecting Nickname already exists on server!");
		return 0;
	    }
	}
	players[player-1] = strdup(s);
	if (teams[player-1])
	    free(teams[player-1]);
	teams[player-1] = NULL;
	player_modes[player-1] = tetrifast;
	send_to(player, "%s %d", tetrifast ? ")#)(!@(*3" : "playernum", player);
	send_to(player, "winlist %s", winlist_str());
	for (i = 1; i <= 6; i++) {
	    if (i != player && players[i-1]) {
		send_to(player, "playerjoin %d %s", i, players[i-1]);
		send_to(player, "team %d %s", i, teams[i-1] ? teams[i-1] : "");
	    }
	}
	if (playing_game) {
	    send_to(player, "ingame");
	    player_lost[player-1] = 1;
	}
	send_to_all_but(player, "playerjoin %d %s", player, players[player-1]);

    } else if (strcmp(cmd, "tetrifaster") == 0) {
	tetrifast = 1;
	goto newplayer;

    } else if (strcmp(cmd, "team") == 0) {
	s = strtok(NULL, " ");
	t = strtok(NULL, "");
	if (!s || atoi(s) != player)
	    return 0;
	if (teams[player])
	    free(teams[player]);
	if (t)
	    teams[player] = strdup(t);
	else
	    teams[player] = NULL;
	send_to_all_but(player, "team %d %s", player, t ? t : "");

    } else if (strcmp(cmd, "pline") == 0) {
	s = strtok(NULL, " ");
	t = strtok(NULL, "");
	if (!s || atoi(s) != player)
	    return 0;
	if (!t)
	    t = "";
	send_to_all_but(player, "pline %d %s", player, t);

    } else if (strcmp(cmd, "plineact") == 0) {
	s = strtok(NULL, " ");
	t = strtok(NULL, "");
	if (!s || atoi(s) != player)
	    return 0;
	if (!t)
	    t = "";
	send_to_all_but(player, "plineact %d %s", player, t);

    } else if (strcmp(cmd, "startgame") == 0) {
	int total;
	char piecebuf[101], specialbuf[101];

	for (i = 1; i < player; i++) {
	    if (player_socks[i-1] >= 0)
		return 1;
	}
	s = strtok(NULL, " ");
	t = strtok(NULL, " ");
	if (!s)
	    return 1;
	i = atoi(s);
	if ((i && playing_game) || (!i && !playing_game))
	    return 1;
	if (!i) {  /* end game */
	    send_to_all("endgame");
	    playing_game = 0;
	    return 1;
	}
	total = 0;
	for (i = 0; i < 7; i++) {
	    if (piecefreq[i])
		memset(piecebuf+total, '1'+i, piecefreq[i]);
	    total += piecefreq[i];
	}
	piecebuf[100] = 0;
	if (total != 100) {
	    send_to_all("plineact 0 cannot start game: Piece frequencies do not total 100 percent!");
	    return 1;
	}
	total = 0;
	for (i = 0; i < 9; i++) {
	    if (specialfreq[i])
		memset(specialbuf+total, '1'+i, specialfreq[i]);
	    total += specialfreq[i];
	}
	specialbuf[100] = 0;
	if (total != 100) {
	    send_to_all("plineact 0 cannot start game: Special frequencies do not total 100 percent!");
	    return 1;
	}
	playing_game = 1;
	game_paused = 0;
	for (i = 1; i <= 6; i++) {
	    if (player_socks[i-1] < 0)
		continue;
	    /* XXX First parameter is stack height */
	    send_to(i, "%s %d %d %d %d %d %d %d %s %s %d %d",
			player_modes[i-1] ? "*******" : "newgame",
			0, initial_level, lines_per_level, level_inc,
			special_lines, special_count, special_capacity,
			piecebuf, specialbuf, level_average, old_mode);
	}
	memset(player_lost, 0, sizeof(player_lost));

    } else if (strcmp(cmd, "pause") == 0) {
	if (!playing_game)
	    return 1;
	s = strtok(NULL, " ");
	if (!s)
	    return 1;
	i = atoi(s);
	if (i)
	    i = 1;	/* to make sure it's not anything else */
	if ((i && game_paused) || (!i && !game_paused))
	    return 1;
	game_paused = i;
	send_to_all("pause %d", i);

    } else if (strcmp(cmd, "playerlost") == 0) {
	if (!(s = strtok(NULL, " ")) || atoi(s) != player)
	    return 1;
	player_loses(player);

    } else if (strcmp(cmd, "f") == 0) {   /* field */
	if (!(s = strtok(NULL, " ")) || atoi(s) != player)
	    return 1;
	if (!(s = strtok(NULL, "")))
	    s = "";
	send_to_all_but(player, "f %d %s", player, s);

    } else if (strcmp(cmd, "lvl") == 0) {
	if (!(s = strtok(NULL, " ")) || atoi(s) != player)
	    return 1;
	if (!(s = strtok(NULL, " ")))
	    return 1;
	levels[player] = atoi(s);
	send_to_all_but(player, "lvl %d %d", player, levels[player]);

    } else if (strcmp(cmd, "sb") == 0) {
	int from, to;
	char *type;

	if (!(s = strtok(NULL, " ")))
	    return 1;
	to = atoi(s);
	if (!(type = strtok(NULL, " ")))
	    return 1;
	if (!(s = strtok(NULL, " ")))
	    return 1;
	from = atoi(s);
	if (from != player)
	    return 1;
	if (to < 0 || to > 6 || player_socks[to-1] < 0 || player_lost[to-1])
	    return 1;
	if (to == 0)
	    send_to_all_but_team(player, "sb %d %s %d", to, type, from);
	else
	    send_to_all_but(player, "sb %d %s %d", to, type, from);

    } else if (strcmp(cmd, "gmsg") == 0) {
	if (!(s = strtok(NULL, "")))
	    return 1;
	send_to_all("gmsg %s", s);

    } else {  /* unrecognized command */
	return 0;

    }

    return 1;
}

/*************************************************************************/
/*************************************************************************/

static void sigcatcher(int sig)
{
    if (sig == SIGHUP) {
	read_config();
	signal(SIGHUP, sigcatcher);
	send_to_all("winlist %s", winlist_str());
    } else if (sig == SIGTERM || sig == SIGINT) {
	quit = 1;
	signal(sig, SIG_IGN);
    }
}

/*************************************************************************/

/* Returns 0 on success, desired program exit code on failure */

static int init()
{
    struct sockaddr_in sin;
#ifdef HAVE_IPV6
    struct sockaddr_in6 sin6;
#endif
    int i;

    /* Set up some sensible defaults */
    *winlist[0].name = 0;
    old_mode = 1;
    initial_level = 1;
    lines_per_level = 2;
    level_inc = 1;
    level_average = 1;
    special_lines = 1;
    special_count = 1;
    special_capacity = 18;
    piecefreq[0] = 14;
    piecefreq[1] = 14;
    piecefreq[2] = 15;
    piecefreq[3] = 14;
    piecefreq[4] = 14;
    piecefreq[5] = 14;
    piecefreq[6] = 15;
    specialfreq[0] = 18;
    specialfreq[1] = 18;
    specialfreq[2] = 3;
    specialfreq[3] = 12;
    specialfreq[4] = 0;
    specialfreq[5] = 16;
    specialfreq[6] = 3;
    specialfreq[7] = 12;
    specialfreq[8] = 18;

    /* (Try to) read the config file */
    read_config();

    /* Catch some signals */
    signal(SIGHUP, sigcatcher);
    signal(SIGINT, sigcatcher);
    signal(SIGTERM, sigcatcher);

    /* Set up a listen socket */
    if (!ipv6_only)
	listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock >= 0){
	i = 1;
	if (setsockopt(listen_sock,SOL_SOCKET,SO_REUSEADDR,&i,sizeof(i))==0) {
	    memset(&sin, 0, sizeof(sin));
	    sin.sin_family = AF_INET;
	    sin.sin_port = htons(31457);
	    if (bind(listen_sock, (struct sockaddr *)&sin, sizeof(sin)) == 0) {
		if (listen(listen_sock, 5) == 0) {
		    goto ipv4_success;
		}
	    }
	}
	i = errno;
	close(listen_sock);
	errno = i;
	listen_sock = -1;
    }
  ipv4_success:

#ifdef HAVE_IPV6
    /* Set up an IPv6 listen socket if possible */
    listen_sock6 = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock6 >= 0) {
	i = 1;
	if (setsockopt(listen_sock6,SOL_SOCKET,SO_REUSEADDR,&i,sizeof(i))==0) {
	    memset(&sin6, 0, sizeof(sin6));
	    sin6.sin6_family = AF_INET6;
	    sin6.sin6_port = htons(31457);
	    if (bind(listen_sock6,(struct sockaddr *)&sin6,sizeof(sin6))==0) {
		if (listen(listen_sock6, 5) == 0) {
		    goto ipv6_success;
		}
	    }
	}
	i = errno;
	close(listen_sock6);
	errno = i;
	listen_sock6 = -1;
    }
  ipv6_success:
#else  /* !HAVE_IPV6 */
    if (ipv6_only) {
	fprintf(stderr,"ipv6_only specified but IPv6 support not available\n");
	return 1;
    }
#endif  /* HAVE_IPV6 */

    if (listen_sock < 0
#ifdef HAVE_IPV6
     && listen_sock6 < 0
#endif
    ) {
	return 1;
    }

    return 0;
}

/*************************************************************************/

static void
decrypt_message(char *buf, char *newbuf, char *iphashbuf)
{
    int j, c, l = strlen(iphashbuf);

    c = xtoi(buf);
    for (j = 2; buf[j] && buf[j+1]; j += 2) {
	int temp, d;

	temp = d = xtoi(buf+j);
	d ^= iphashbuf[((j/2)-1) % l];
	d += 255 - c;
	d %= 255;
	newbuf[j/2-1] = d;
	c = temp;
    }
    newbuf[j/2-1] = 0;
}

static void check_sockets()
{
    fd_set fds;
    int i, fd, maxfd;

    FD_ZERO(&fds);
    if (listen_sock >= 0)
	FD_SET(listen_sock, &fds);
    maxfd = listen_sock;
#ifdef HAVE_IPV6
    if (listen_sock6 >= 0)
	FD_SET(listen_sock6, &fds);
    if (listen_sock6 > maxfd)
	maxfd = listen_sock6;
#endif
    for (i = 0; i < 6; i++) {
	if (player_socks[i] != -1) {
	    if (player_socks[i] < 0)
		fd = (~player_socks[i]) - 1;
	    else
		fd = player_socks[i];
	    FD_SET(fd, &fds);
	    if (fd > maxfd)
		maxfd = fd;
	}
    }

    if (select(maxfd+1, &fds, NULL, NULL, NULL) <= 0)
	return;

    if (listen_sock >= 0 && FD_ISSET(listen_sock, &fds)) {
	struct sockaddr_in sin;
	int len = sizeof(sin);
	fd = accept(listen_sock, (struct sockaddr *)&sin, &len);
	if (fd >= 0) {
	    for (i = 0; i < 6 && player_socks[i] != -1; i++)
		;
	    if (i == 6) {
		sockprintf(fd, "noconnecting Too many players on server!");
		close(fd);
	    } else {
		player_socks[i] = ~(fd+1);
		memcpy(player_ips[i], &sin.sin_addr, 4);
	    }
	}
    } /* if (FD_ISSET(listen_sock)) */

#ifdef HAVE_IPV6
    if (listen_sock6 >= 0 && FD_ISSET(listen_sock6, &fds)) {
	struct sockaddr_in6 sin6;
	int len = sizeof(sin6);
	fd = accept(listen_sock6, (struct sockaddr *)&sin6, &len);
	if (fd >= 0) {
	    for (i = 0; i < 6 && player_socks[i] != -1; i++)
		;
	    if (i == 6) {
		sockprintf(fd, "noconnecting Too many players on server!");
		close(fd);
	    } else {
		player_socks[i] = ~(fd+1);
		memcpy(player_ips[i], (char *)(&sin6.sin6_addr)+12, 4);
	    }
	}
    } /* if (FD_ISSET(listen_sock6)) */
#endif

    for (i = 0; i < 6; i++) {
	char buf[1024];

	if (player_socks[i] == -1)
	    continue;
	if (player_socks[i] < 0)
	    fd = (~player_socks[i]) - 1;
	else
	    fd = player_socks[i];
	if (!FD_ISSET(fd, &fds))
	    continue;
	sgets(buf, sizeof(buf), fd);

	if (player_socks[i] < 0) {
	    /* Our extension: the client can give up on the meaningless
	     * encryption completely. */
	    if (strncmp(buf,"tetrisstart ",12) != 0) {
		/* Messy decoding stuff */
		char iphashbuf[16], newbuf[1024];
		unsigned char *ip;
#ifndef NO_BRUTE_FORCE_DECRYPTION
		int hashval;
#endif

		if (strlen(buf) < 2*13) {  /* "tetrisstart " + initial byte */
		    close(fd);
		    player_socks[i] = -1;
		    continue;
		}

		ip = player_ips[i];
		sprintf(iphashbuf, "%d",
			ip[0]*54 + ip[1]*41 + ip[2]*29 + ip[3]*17);
		decrypt_message(buf, newbuf, iphashbuf);
		if(strncmp(newbuf,"tetrisstart ",12) == 0)
		    goto cryptok;

#ifndef NO_BRUTE_FORCE_DECRYPTION
		/* The IP-based crypt does not work for clients behind NAT. So
		 * help them by brute-forcing the crypt. This should not be
		 * even noticeable unless you are running this under ucLinux on
		 * some XT machine. */
		for (hashval = 0; hashval < 35956; hashval++) {
		    sprintf(iphashbuf, "%d", hashval);
		    decrypt_message(buf, newbuf, iphashbuf);
		    if(strncmp(newbuf,"tetrisstart ",12) == 0)
			goto cryptok;
		} /* for (hashval) */
#endif

		if (strncmp(newbuf, "tetrisstart ", 12) != 0) {
		    close(fd);
		    player_socks[i] = -1;
		    continue;
		}

cryptok:
		/* Buffers should be the same size, but let's be paranoid */
		strncpy(buf, newbuf, sizeof(buf));
		buf[sizeof(buf)-1] = 0;
	    } /* if encrypted */
	    player_socks[i] = fd;  /* Has now registered */
	} /* if client not registered */

	if (!server_parse(i+1, buf)) {
	    close(fd);
	    player_socks[i] = -1;
	    if (players[i]) {
		send_to_all("playerleave %d", i+1);
		if (playing_game)
		    player_loses(i+1);
		free(players[i]);
		players[i] = NULL;
		if (teams[i]) {
		    free(teams[i]);
		    teams[i] = NULL;
		}
	    }
	}
    } /* for each player socket */
}

/*************************************************************************/

#ifdef SERVER_ONLY
int main()
#else
int server_main()
#endif
{
    int i;

    if ((i = init()) != 0)
	return i;
    while (!quit)
	check_sockets();
    write_config();
    if (listen_sock >= 0)
	close(listen_sock);
#ifdef HAVE_IPV6
    if (listen_sock6 >= 0)
	close(listen_sock6);
#endif
    for (i = 0; i < 6; i++)
	close(player_socks[i]);
    return 0;
}

/*************************************************************************/
