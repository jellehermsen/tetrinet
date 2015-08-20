/* Tetrinet for Linux, by Andrew Church <achurch@achurch.org>
 * This program is public domain.
 *
 * Tetris constants and routine declarations.
 */

#ifndef TETRIS_H
#define TETRIS_H

/*************************************************************************/

#define PIECE_BAR	0	/* Straight bar */
#define PIECE_SQUARE	1	/* Square block */
#define PIECE_L_REVERSE	2	/* Reversed L block */
#define PIECE_L		3	/* L block */
#define PIECE_Z		4	/* Z block */
#define PIECE_S		5	/* S block */
#define PIECE_T		6	/* T block */

#define SPECIAL_A	0	/* Add line */
#define SPECIAL_C	1	/* Clear line */
#define SPECIAL_N	2	/* Nuke field */
#define SPECIAL_R	3	/* Clear random blocks */
#define SPECIAL_S	4	/* Switch fields */
#define SPECIAL_B	5	/* Clear special blocks */
#define SPECIAL_G	6	/* Block gravity */
#define SPECIAL_Q	7	/* Blockquake */
#define SPECIAL_O	8	/* Block bomb */

/*************************************************************************/

#define MAX_SPECIALS	64

extern int piecefreq[7], specialfreq[9];
extern int old_mode;
extern int initial_level, lines_per_level, level_inc, level_average;
extern int special_lines, special_count, special_capacity;
extern Field fields[6];
extern int levels[6];
extern int lines;
extern signed char specials[MAX_SPECIALS];
extern int next_piece;
extern int current_x, current_y;


typedef struct {
    int hot_x, hot_y;	/* Hotspot coordinates */
    int top, left;	/* Top-left coordinates relative to hotspot */
    int bottom, right;	/* Bottom-right coordinates relative to hotspot */
    char shape[4][4];	/* Shape data for the piece */
} PieceData;

PieceData piecedata[7][4];

extern int current_piece, current_rotation;


extern void init_shapes(void);
extern int get_shape(int piece, int rotation, char buf[4][4]);

extern void new_game(void);

extern void new_piece(void);
extern void step_down(void);
extern void do_special(const char *type, int from, int to);

extern int tetris_timeout(void);
extern void tetris_timeout_action(void);
extern void tetris_input(int c);

/*************************************************************************/

#endif	/* TETRIS_H */
