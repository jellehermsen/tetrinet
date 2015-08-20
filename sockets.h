/* Tetrinet for Linux, by Andrew Church <achurch@achurch.org>
 * This program is public domain.
 *
 * Socket routine declarations.
 */

#ifndef SOCKETS_H
#define SOCKETS_H

extern int sgetc(int s);
extern int sungetc(int c, int s);
extern char *sgets(char *buf, int len, int s);
extern int sputs(const char *buf, int len);
extern int sockprintf(int s, const char *fmt, ...);
extern int conn(const char *host, int port, char ipbuf[4]);
extern void disconn(int s);

#endif	/* SOCKETS_H */
