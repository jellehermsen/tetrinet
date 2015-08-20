/* Tetrinet for Linux, by Andrew Church <achurch@achurch.org>
 * This program is public domain.
 *
 * Socket routines.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include "sockets.h"
#include "tetrinet.h"

static FILE *logfile;

/*************************************************************************/

static int lastchar = EOF;

int sgetc(int s)
{
    int c;
    char ch;

    if (lastchar != EOF) {
	c = lastchar;
	lastchar = EOF;
	return c;
    }
    if (read(s, &ch, 1) != 1)
	return EOF;
    c = ch & 0xFF;
    return c;
}

int sungetc(int c, int s)
{
    return lastchar = c;
}

/*************************************************************************/

/* Read a string, stopping with (and discarding) 0xFF as line terminator.
 * If connection was broken, return NULL.
 */

char *sgets(char *buf, int len, int s)
{
    int c;
    unsigned char *ptr = (unsigned char *) buf;

    if (len == 0)
	return NULL;
    c = sgetc(s);
    while (--len && (*ptr++ = c) != 0xFF && (c = sgetc(s)) >= 0)
	;
    if (c < 0)
	return NULL;
    if (c == 0xFF)
	ptr--;
    *ptr = 0;
    if (log) {
	if (!logfile)
	    logfile = fopen(logname, "a");
	if (logfile) {
	    struct timeval tv;
	    gettimeofday(&tv, NULL);
	    fprintf(logfile, "[%d.%03d] <<< %s\n",
			(int) tv.tv_sec, (int) tv.tv_usec/1000, buf);
	    fflush(logfile);
	}
    }
    return buf;
}

/*************************************************************************/

/* Adds a 0xFF line terminator. */

int sputs(const char *str, int s)
{
    unsigned char c = 0xFF;
    int n = 0;

    if (log) {
	if (!logfile)
	    logfile = fopen(logname, "a");
	if (logfile) {
	    struct timeval tv;
	    gettimeofday(&tv, NULL);
	    fprintf(logfile, "[%d.%03d] >>> %s\n",
			(int) tv.tv_sec, (int) tv.tv_usec/1000, str);
	}
    }
    if (*str != 0) {
	n = write(s, str, strlen(str));
	if (n <= 0)
	    return n;
    }
    if (write(s, &c, 1) <= 0)
	return n;
    return n+1;
}

/*************************************************************************/

/* Adds a 0xFF line terminator. */

int sockprintf(int s, const char *fmt, ...)
{
    va_list args;
    char buf[16384];	/* Really huge, to try and avoid truncation */

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    return sputs(buf, s);
}

/*************************************************************************/
/*************************************************************************/

int conn(const char *host, int port, char ipbuf[4])
{
#ifdef HAVE_IPV6
    char hbuf[NI_MAXHOST];
    struct addrinfo hints, *res, *res0;
    char service[11];
#else
    struct hostent *hp;
    struct sockaddr_in sa;
#endif
    int sock = -1;

#ifdef HAVE_IPV6
    snprintf(service, sizeof(service), "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, service, &hints, &res0))
	return -1;
    for (res = res0; res; res = res->ai_next) {
	int errno_save;
	sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (sock < 0)
	    continue;
	getnameinfo(res->ai_addr, res->ai_addrlen, hbuf, sizeof(hbuf),
		    NULL, 0, 0);
	if (connect(sock, res->ai_addr, res->ai_addrlen) == 0) {
	    if (ipbuf) {
		if (res->ai_family == AF_INET6) {
		    struct sockaddr_in6 *sin6 =
			(struct sockaddr_in6 *)(res->ai_addr);
		    memcpy(ipbuf, (char *)(&sin6->sin6_addr) + 12, 4);
		} else {
		    struct sockaddr_in *sin =
			(struct sockaddr_in *)(res->ai_addr);
		    memcpy(ipbuf, &sin->sin_addr, 4);
		}
	    }
	    break;
	}
	errno_save = errno;
	close(sock);
	sock = -1;
	errno = errno_save;
    }
    freeaddrinfo(res0);
#else  /* !HAVE_IPV6 */
    memset(&sa, 0, sizeof(sa));
    if (!(hp = gethostbyname(host)))
	return -1;
    memcpy((char *)&sa.sin_addr, hp->h_addr, hp->h_length);
    sa.sin_family = hp->h_addrtype;
    sa.sin_port = htons((unsigned short)port);
    if ((sock = socket(sa.sin_family, SOCK_STREAM, 0)) < 0)
	return -1;
    if (connect(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
	int errno_save = errno;
	close(sock);
	errno = errno_save;
	return -1;
    }
    if (ipbuf)
	memcpy(retbuf, &sa.sin_addr, 4);
#endif

    return sock;
}

/*************************************************************************/

void disconn(int s)
{
    shutdown(s, 2);
    close(s);
}

/*************************************************************************/
