/* Tetrinet for Linux, by Andrew Church <achurch@achurch.org>
 * This program is public domain.
 *
 * Tetrinet main include file.
 */

#ifndef TETRINET_H
#define TETRINET_H

#ifndef IO_H
# include "io.h"
#endif

/*************************************************************************/

/* Basic types */

#define FIELD_WIDTH	12
#define FIELD_HEIGHT	22
typedef char Field[FIELD_HEIGHT][FIELD_WIDTH];

typedef struct {
    char name[32];
    int team;	/* 0 = individual player, 1 = team */
    int points;
    int games;	/* Number of games played */
} WinInfo;
#define MAXWINLIST	64	/* Maximum size of winlist */
#define MAXSENDWINLIST	10	/* Maximum number of winlist entries to send
				 *    (this avoids triggering a buffer
				 *    overflow in Windows Tetrinet 1.13) */
#define MAXSAVEWINLIST	32	/* Maximum number of winlist entries to save
				 *    (this allows new players to get into
				 *    a winlist with very high scores) */

/*************************************************************************/

/* Overall display modes */

#define MODE_FIELDS	0
#define MODE_PARTYLINE	1
#define MODE_WINLIST	2
#define MODE_SETTINGS	3
#define MODE_CLIENT	4	/* Client settings */
#define MODE_SERVER	5	/* Server settings */

/*************************************************************************/

/* Key definitions for input.  We use K_* to avoid conflict with ncurses */

#define K_UP		0x100
#define K_DOWN		0x101
#define K_LEFT		0x102
#define K_RIGHT		0x103
#define K_F1		0x104
#define K_F2		0x105
#define K_F3		0x106
#define K_F4		0x107
#define K_F5		0x108
#define K_F6		0x109
#define K_F7		0x10A
#define K_F8		0x10B
#define K_F9		0x10C
#define K_F10		0x10D
#define K_F11		0x10E
#define K_F12		0x10F

/* For function keys that don't correspond to something above, i.e. that we
 * don't care about: */
#define K_INVALID	0x7FFF

/*************************************************************************/

/* Externs */

extern int fancy;
extern int log;
extern char *logname;
extern int windows_mode;
extern int noslide;
extern int tetrifast;
extern int cast_shadow;

extern int my_playernum;
extern WinInfo winlist[MAXWINLIST];
extern int server_sock;
extern int dispmode;
extern char *players[6];
extern char *teams[6];
extern int playing_game;
extern int not_playing_game;
extern int game_paused;

extern Interface *io;

/*************************************************************************/

#endif
