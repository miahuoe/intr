/*
Copyright (c) 2019 Michał Czarnecki

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "terminal.h"

char *special_type_str[] = {
	[S_NONE] = "none",
	[S_ARROW_UP] = "up",
	[S_ARROW_DOWN] = "down",
	[S_ARROW_RIGHT] = "right",
	[S_ARROW_LEFT] = "left",
	[S_HOME] = "home",
	[S_END] = "end",
	[S_PAGE_UP] = "pup",
	[S_PAGE_DOWN] = "pdn",
	[S_INSERT] = "ins",
	[S_BACKSPACE] = "bsp",
	[S_DELETE] = "del",
	[S_ESCAPE] = "esc",
};

/* TODO more */
s2s seq2special[] = {
	{ "\x1b[@", S_INSERT },
	{ "\x1b[A", S_ARROW_UP },
	{ "\x1b[B", S_ARROW_DOWN },
	{ "\x1b[C", S_ARROW_RIGHT },
	{ "\x1b[D", S_ARROW_LEFT },
	{ "\x1b[H", S_HOME },
	{ "\x1b[F", S_END },
	{ "\x1b[P", S_DELETE },
	{ "\x1b[V", S_PAGE_UP },
	{ "\x1b[U", S_PAGE_DOWN },
	{ "\x1b[Y", S_END },

	{ "\x1bOA", S_ARROW_UP },
	{ "\x1bOB", S_ARROW_DOWN },
	{ "\x1bOC", S_ARROW_RIGHT },
	{ "\x1bOD", S_ARROW_LEFT },
	{ "\x1bOH", S_HOME },
	{ "\x1bOF", S_END },

	{ "\x1b[1~", S_HOME },
	{ "\x1b[3~", S_DELETE },
	{ "\x1b[4~", S_END },
	{ "\x1b[5~", S_PAGE_UP },
	{ "\x1b[6~", S_PAGE_DOWN },
	{ "\x1b[7~", S_HOME },
	{ "\x1b[8~", S_END },
	{ "\x1b[4h", S_INSERT },
	{ "\x1b", S_ESCAPE },
	{ NULL, S_NONE },
};

static size_t _striint(char *buf, int i, _Bool *tail)
{
	char c;
	size_t s;

	if (i == 0) {
		*tail = 1;
		return 0;
	}
	c = '0' + (i % 10);
	i /= 10;
	s = _striint(buf, i, tail);
	buf += s;
	*buf = c;
	return s+1;
}

static size_t strint(char *buf, int i)
{
	_Bool tail = 0;
	if (i == 0) {
		*buf = '0';
		return 1;
	}
	else {
		return _striint(buf, i, &tail);
	}
}

void set_cur_pos(int fd, int x, int y)
{
	char buf[2+5+1+5+1];
	char *b = buf;
	size_t xs, ys;

	b[0] = '\x1b';
	b[1] = '[';
	b += 2;
	ys = strint(b, y);
	b += ys;
	b[0] = ';';
	b += 1;
	xs = strint(b, x);
	b += xs;
	b[0] = 'H';
	write(fd, buf, 2+ys+1+xs+1);
}

static char read1(char *c, int fd)
{
	if (1 == read(fd, c, 1)) return *c;
	return 0;
}

void get_cur_pos(int fd, int *x, int *y)
{
	char c;

	write(fd, "\x1b[6n", 4);
	*x = *y = 0;
	if (read1(&c, fd) == '\x1b' && read1(&c, fd) == '[' && read1(&c, fd)) {
		while ('0' <= c && c <= '9') {
			*y *= 10;
			*y += c - '0';
			read1(&c, fd);
		}
		if (c != ';') {
			*x = *y = 0;
			/* TODO */
		}
		read1(&c, fd);
		while ('0' <= c && c <= '9') {
			*x *= 10;
			*x += c - '0';
			read1(&c, fd);
		}
		if (c != 'R') {
			*x = *y = 0;
			/* TODO */
		}
	}
}

void get_win_dims(int fd, int *C, int *R)
{
	struct winsize ws;
	if (ioctl(fd, TIOCGWINSZ, &ws) == -1) {
		*R = *C = 0;
	}
	*R = ws.ws_row;
	*C = ws.ws_col;
}

int move_cursor(int fd, int R, int C) {
	char buf[1+1+5+1+5+1];
	char *b = buf;
	size_t rs, cs;

	b[0] = '\x1b';
	b[1] = '[';
	b += 2;
	rs = strint(b, R);
	b += rs;
	b[0] = ';';
	b += 1;
	cs = strint(b, C);
	b += cs;
	b[0] = 'H';
	return (write(fd, buf, 2+rs+1+cs+1) == -1 ? errno : 0);
}

int raw(struct termios* before, int fd)
{
	struct termios raw;

	if (tcgetattr(fd, before)) return errno;
	raw = *before;
	cfmakeraw(&raw);
	//raw.c_cc[VINTR] = 0x03;
	//raw.c_cc[VSUSP] = 0x1a;
	raw.c_iflag &= ~(BRKINT);
	raw.c_lflag &= ~(ISIG); /* Ignore Ctrl-C and Ctrl-Z */
	return (tcsetattr(fd, TCSAFLUSH, &raw) ? errno : 0);
}

int unraw(struct termios* before, int fd)
{
	if (tcsetattr(fd, TCSAFLUSH, before)) return errno;
	return 0;
}

ssize_t tread(int fd, void *buf, size_t num, suseconds_t to)
{
	fd_set s;
	ssize_t n;
	int e;
	struct timeval t;

	FD_ZERO(&s);
	FD_SET(fd, &s);

	do {
		errno = 0;
		t.tv_sec = 0;
		t.tv_usec = to;
		n = select(fd+1, &s, 0, 0, &t);
	} while (n == -1 && ((e = errno), e == EINTR));
	if (n == -1 || n == 0) {
		goto exit;
	}
	do {
		errno = 0;
		n = read(fd, buf, num);
	} while (n == -1 && ((e = errno), e == EAGAIN || e == EINTR));
exit:
	FD_ZERO(&s);
	return n;
}

static _Bool in(char c, char *S)
{
	while (*S && c != *S) S++;
	return *S;
}

static char tread1(char *c, int fd, suseconds_t to)
{
	if (1 == tread(fd, c, 1, to)) return *c;
	return 0;
}

special_type which_key(char *seq)
{
	int i = 0;
	while (seq2special[i].seq && seq2special[i].t != S_NONE) {
		if (!strcmp(seq2special[i].seq, seq)) {
			return seq2special[i].t;
		}
		i++;
	}
	return IT_NONE;
}

/* TODO BUG
 * Quick enough ESC + arrow left (for example) segfaults
 *
 * TODO error prone (ifs arent 'elsed')
 */
input get_input(int fd)
{
	input i;
	suseconds_t t = 250000;
	int utflen, b;
	char seq[8] = { 0 };

	memset(&i, 0, sizeof(i));
	if (read1(seq, fd) == 0) {
		i.t = IT_NONE;
	}
	else if (seq[0] == '\x1b') {
		retry:
		if (tread1(seq+1, fd, t) && in(seq[1], "[O")) {
			if (read1(seq+2, fd) && in(seq[2], "0123456789")) {
				read1(seq+3, fd);
			}
			else if (in(seq[2], "ABCDHFPVUY")) {
			}
			else {
				i.t = IT_NONE;
				return i;
			}
		}
		else if (seq[1] == '\x1b') {
			goto retry; /* Fixes fast-esc bug. But is it a good solution? */
		}
		else {
			i.t = IT_SPEC;
			i.s = S_ESCAPE;
			return i;
		}
		i.t = IT_SPEC;
		i.s = which_key(seq);
	}
	else if (seq[0] == 0x7f) {
		i.t = IT_SPEC;
#if defined(__linux__) || defined(__linux) || defined(linux)
		i.s = S_BACKSPACE;
#else
		i.s = S_DELETE;
#endif
	}
	else if (!(seq[0] & 0x60)) {
		i.t = IT_CTRL;
		i.utf[0] = seq[0] | 0x40;
	}
	else if ((utflen = utf8_b2len(seq))) {
		i.t = IT_UTF8;
		i.utf[0] = seq[0];
		for (b = 1; b < utflen; ++b) {
			if (!read1(i.utf+b, fd)) {
				i.t = IT_NONE;
				memset(i.utf, 0, 5);
				return i;
			}
		}
		for (; b < 5; ++b) {
			i.utf[b] = 0;
		}
	}
	return i;
}
