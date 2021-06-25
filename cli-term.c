/*
 * Terminal Input/Output Engine
 *
 * Copyright (c) 2021 Jiajia Liu <liujia6264@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/uio.h>
#include <ctype.h>
#include <arpa/telnet.h>

#include "cli-term.h"
#include "event-loop.h"
#include "stream.h"
#include "hashtable.h"

#define CTRL(c)		(c - '@')
#define CTRL_BACKSPACE	CTRL('H')
#define CTRL_DEL	0x7f

#define TERM_NORMAL     0
#define TERM_PRE_ESCAPE 1  /* Esc seen or Alt + X */
#define TERM_ESCAPE     2  /* ANSI terminal escape (Esc-[) seen */
#define TERM_LITERAL    3  /* Next char taken as literal */
#define ESCAPE		0x1b
#define TERM_MAXHIST	32

#define TERM_DEFAULT_NAME	"Chaconne"

struct buffer {
	char buf[8192];
	int cp, len, max;
};

struct history {
	char *buf[TERM_MAXHIST];
	int cp, index;
};

static size_t string_hash(const void *data)
{
	size_t hash = 0;
	const char *p;

	for (p = data; *p; p++) {
		hash = hash * 31 + *p;
	}

	return hash;
}

static int string_compare(const void *a, const void *b)
{
	return strcmp(a, b);
}

struct cmdopt *cmdopt_create(void)
{
	struct cmdopt *opt;
	struct kpattr attr = {
		.key_is_ptr = 1,
		.value_is_ptr = 1,

		.hash = string_hash,
		.compare = string_compare,
		.free_key = NULL,
		.free_value = NULL,
	};

	opt = malloc(sizeof(*opt));
	if (!opt)
		return NULL;

	opt->argc = 0;
	opt->kpairs = hashtable_create(32, &attr);
	if (!opt->kpairs) {
		free(opt);
		return NULL;
	}

	return opt;
}

void cmdopt_clear(struct cmdopt *opt)
{
	hashtable_clear(opt->kpairs);
	opt->argc = 0;
}

void cmdopt_destroy(struct cmdopt *opt)
{
	if (opt) {
		hashtable_destroy(opt->kpairs);
		free(opt);
	}
}

struct term {
	int fd;

	struct buffer *in;
	struct stream *out;
	struct history *hist;

	int escape;
	int stop;

	const char *name;

	struct event_loop *loop;
	struct event_source *source;
	struct event_source *signals;

	struct cmd_node *cmd_tree;
	struct cmdopt *cmdopt;
};

const char *history_previous(struct history *hist)
{
	int try_index;

	try_index = hist->cp;
	if (try_index == 0)
		try_index = TERM_MAXHIST - 1;
	else
		try_index--;

	if (try_index == hist->index)
		return NULL;

	if (hist->buf[try_index] == NULL)
		return NULL;

	hist->cp = try_index;
	return hist->buf[try_index];
}

const char *history_next(struct history *hist)
{
	int try_index;

	if (hist->cp == hist->index)
		return NULL;

	try_index = hist->cp;
	if (try_index == (TERM_MAXHIST - 1))
		try_index = 0;
	else
		try_index++;

	if (try_index == hist->index) {
		hist->cp = hist->index;
		return "";
	}

	if (hist->buf[try_index] == NULL)
		return NULL;

	hist->cp = try_index;
	return hist->buf[try_index];
}

static void history_add(struct history *hist, const char *line)
{
	int index;

	if (!line || line[0] == '\0')
		return;

	index = hist->index ? hist->index - 1 : TERM_MAXHIST - 1;

	if (hist->buf[index]) {
		if (strcmp(line, hist->buf[index]) == 0) {
			hist->cp = hist->index;
			return;
		}
	}

	if (hist->buf[hist->index])
		free(hist->buf[hist->index]);

	hist->buf[hist->index] = strdup(line);
	if (hist->buf[hist->index] == NULL)
		return;

	hist->index++;
	if (hist->index == TERM_MAXHIST)
		hist->index = 0;

	hist->cp = hist->index;
}

static struct history *history_create(void)
{
	struct history *hist;

	hist = calloc(1, sizeof(*hist));
	if (hist == NULL)
		return NULL;

	return hist;
}

static void history_destroy(struct history *hist)
{
	if (hist) {
		int i;

		for (i = 0; i < TERM_MAXHIST; i++) {
			if (hist->buf[i]) {
				free(hist->buf[i]);
			}
		}
		free(hist);
	}
}

static void history_print(struct history *hist, struct stream *out)
{
	int index;

	for (index = hist->index + 1; index != hist->index;) {
		if (index == TERM_MAXHIST) {
			index = 0;
			continue;
		}

		if (hist->buf[index] != NULL)
			stream_puts(out, "  %s\n", hist->buf[index]);

		index++;
	}
}

void term_show_history(struct term *term)
{
	history_print(term->hist, term->out);
}

void term_quit(struct term *term)
{
	term->stop = 1;
}

int term_want_exit(struct term *term)
{
	return term->stop;
}

struct stream *term_ostream(struct term *term)
{
	return term->out;
}

struct cmdopt *term_cmdopt(struct term *term)
{
	return term->cmdopt;
}

struct cmd_node *term_cmd_tree(struct term *term)
{
	return term->cmd_tree;
}

static void term_prompt(struct term *term)
{
	stream_puts(term->out, "%s > ", term->name);
}

static void term_read(struct term *term, int c);

static int term_handle_input(int fd, uint32_t mask, void *data)
{
	char c;
	struct term *term = data;
	struct event_source *source = term->source;

	if (mask & EVENT_HANGUP)
		mask |= (EVENT_WRITABLE | EVENT_READABLE);

	if (mask & EVENT_WRITABLE) {
		term_flush(term);
		event_source_fd_update(source, EVENT_READABLE);
	}

	if (mask & EVENT_READABLE) {
		if (read(fd, &c, 1) != 1)
			exit(1);

		term_read(term, c);
		event_source_fd_update(source, EVENT_WRITABLE);
	}

	return 0;
}

#if 0
static void term_hello(struct term *term)
{
	if (strcmp(term->name, TERM_DEFAULT_NAME) != 0)
		term_print(term, "\nWelcom to %s Terminal.\n", term->name);
	else
		term_print(term, "\nWelcom to System Terminal.\n");
}
#endif

static void term_help_prompt(struct term *term)
{
	term_print(term, "\r\n? - help, TAB - completion, list - all commands\r\n\r\n");
}

void term_will_echo(struct term *term)
{
	uint8_t cmd[] = { IAC, WILL, TELOPT_ECHO };

	stream_put(term->out, cmd, sizeof(cmd));
}

void term_will_suppress_go_ahead(struct term *term)
{
	uint8_t cmd[] = { IAC, WILL, TELOPT_SGA };

	stream_put(term->out, cmd, sizeof(cmd));
}

void term_dont_linemode(struct term *term)
{
	uint8_t cmd[] = { IAC, DONT, TELOPT_LINEMODE };

	stream_put(term->out, cmd, sizeof(cmd));
}

void term_do_window_size(struct term *term)
{
	uint8_t cmd[] = { IAC, DO, TELOPT_NAWS };

	stream_put(term->out, cmd, sizeof(cmd));
}

int term_fd(struct term *term)
{
	return term->fd;
}

struct term *term_create(struct event_loop *loop, int fd, const char *name)
{
	struct term *term;

	term = calloc(1, sizeof(*term));
	if (term == NULL)
		return term;

	term->name = name;
	if (!term->name || term->name[0] == '\0')
		term->name = TERM_DEFAULT_NAME;

	term->fd = fd;
	term->in = calloc(1, sizeof(struct buffer));
	if (term->in == NULL)
		goto err_in_buf;
	term->in->max = sizeof(term->in->buf);

	term->out = stream_new();
	if (term->out == NULL)
		goto err_out_buf;

	term->hist = history_create();
	if (term->hist == NULL)
		goto err_history;

	term->loop = loop;
	term->source = event_loop_add_fd(term->loop, fd, 1, EVENT_WRITABLE,
					 term_handle_input, term);
	if (term->source == NULL)
		goto err_event_source;

	term->cmdopt = cmdopt_create();
	if (!term->cmdopt)
		goto err_cmdopt;

	term->cmd_tree = cmd_tree_build_default();

	if (fd != STDIN_FILENO) {
		//term_will_echo(term);
		//term_will_suppress_go_ahead(term);
		//term_dont_linemode(term);
	}

	term_help_prompt(term);
	term_prompt(term);

	return term;

err_cmdopt:
	event_source_remove(term->source);
err_event_source:
	history_destroy(term->hist);
err_history:
	stream_free(term->out);
err_out_buf:
	free(term->in);
err_in_buf:
	free(term);

	return NULL;

}

void term_run(struct term *term)
{
	while (!term->stop) {
		event_loop_dispatch(term->loop, -1);
	}
}

void term_destroy(struct term *term)
{
	cmd_tree_delete(term->cmd_tree);
	cmdopt_destroy(term->cmdopt);
	event_source_remove(term->source);
	history_destroy(term->hist);
	stream_free(term->out);
	free(term->in);
	free(term);
}

static void term_backward_char(struct term *term)
{
	if (term->in->cp) {
		term->in->cp--;
		stream_putc(term->out, '\b');
	}
}

static void term_forward_char(struct term *term)
{
	if (term->in->cp < term->in->len) {
		stream_putc(term->out, term->in->buf[term->in->cp]);
		term->in->cp++;
	}
}

static void term_delete_backward_char(struct term *term)
{
	struct buffer *in = term->in;
	struct stream *out = term->out;

	if (in->cp) {
		int i, mv;

		for (i = in->cp; i < in->len; i++)
			in->buf[i - 1] = in->buf[i];

		in->cp--;
		in->len--;
		in->buf[in->len] = '\0';

		mv = in->len - in->cp;
		stream_putc(out, '\b');
		stream_putstrn(out, &in->buf[in->cp], mv);
		stream_putc(out, ' ');
		for (i = 0; i <= mv; i++)
			stream_putc(out, '\b');
	}

}

static void term_delete_char(struct term *term)
{
	struct buffer *in = term->in;
	struct stream *out = term->out;

	if (in->cp < in->len) {
		int i, mv;

		for (i = in->cp + 1; i < in->len; i++)
			in->buf[i - 1] = in->buf[i];
		in->len--;
		in->buf[in->len] = '\0';

		mv = in->len - in->cp;
		stream_putstrn(out, &in->buf[in->cp], mv);
		stream_putc(out, ' ');
		for (i = 0; i <= mv; i++)
			stream_putc(out, '\b');
	}
}

static void term_self_insert(struct term *term, int c)
{
	int i, mv;
	struct buffer *in = term->in;
	struct stream *out = term->out;

	if (in->len + 1 >= in->max)
		return;

	if (in->cp < in->len) {
		for (i = in->len + 1; i > in->cp; i--)
			in->buf[i] = in->buf[i - 1];
	}

	in->buf[in->cp] = c;
	in->cp++;
	in->len++;
	in->buf[in->len] = '\0';

	mv = in->len - in->cp;

	stream_putc(out, c);
	if (mv) {
		stream_putstrn(out, &in->buf[in->cp], mv);
		for (i = 0; i < mv; i++)
			stream_putc(out, '\b');
	}
}

static void term_execute(struct term *term)
{
	stream_puts(term->out, "\r\n");
	term_flush(term);
	cmd_execute(term, term->cmd_tree, term->in->buf);
	history_add(term->hist, term->in->buf);

	term->in->cp = 0;
	term->in->len = 0;
	term->in->buf[0] = '\0';

	term_prompt(term);
}

int term_print(struct term *term, const char *fmt, ...)
{
	int l;
	va_list ap;

	va_start(ap, fmt);
	l = stream_vputs(term->out, fmt, ap);
	va_end(ap);

	return l;
}

int term_flush(struct term *term)
{
	return stream_flush(term->out, 1);
}

static void term_redraw_line(struct term *term)
{
	stream_put(term->out, term->in->buf, term->in->len);
	term->in->cp = term->in->len;
}

static void term_backward_pure_word(struct term *term)
{
	struct buffer *in = term->in;

	while (in->cp > 0 && in->buf[in->cp - 1] != ' ')
		term_backward_char(term);
}

static void term_insert_word_overwrite(struct term *term, char *str)
{
	size_t n = strlen(str);
	struct buffer *in = term->in;

	if (n > in->max - in->cp - 1)
		n = in->max - in->cp - 1;

	memcpy(&in->buf[in->cp], str, n);
	in->cp += n;
	in->len = in->cp;
	in->buf[in->len] = '\0';
	stream_putstrn(term->out, str, n);
}

static void term_complete_command(struct term *term)
{
	int ret;
	int num = 0;
	char **keys = NULL;

	stream_puts(term->out, "\r\n");

	ret = cmd_complete(term->cmd_tree, term->in->buf, &num, &keys);
	if (ret == CMD_ERR_NO_MATCH) {
		stream_puts(term->out, "%% No matched command.\r\n");
		term_prompt(term);
		term_redraw_line(term);
	} else if (ret == CMD_COMPLETE_FULL_MATCH) {
		term_prompt(term);
		term_redraw_line(term);
		term_backward_pure_word(term);
		term_insert_word_overwrite(term, keys[0]);
		term_self_insert(term, ' ');
		cmd_complete_free(ret, keys);
	} else if (ret == CMD_COMPLETE_MATCH) {
		term_prompt(term);
		term_redraw_line(term);
		term_backward_pure_word(term);
		term_insert_word_overwrite(term, keys[0]);
		cmd_complete_free(ret, keys);
	} else if (ret == CMD_COMPLETE_LIST_MATCH) {
		int i;
		for (i = 0; i < num; i++) {
			if (i && (i % 5) == 0)
				stream_puts(term->out, "\r\n");
			stream_puts(term->out, "%-16s", keys[i]);
		}
		stream_puts(term->out, "\r\n");
		term_prompt(term);
		term_redraw_line(term);
		cmd_complete_free(ret, keys);
	} else {
		term_prompt(term);
		term_redraw_line(term);
	}
}

static void term_describe_command(struct term *term)
{
	int ret, num = 0, cr = 0;
	char **keys = NULL, **descs = NULL;

	ret = cmd_describe(term->cmd_tree, term->in->buf, &num, &keys, &descs, &cr);

	stream_puts(term->out, "\r\n");

	if (ret == CMD_ERR_NO_MATCH) {
		stream_puts(term->out, "%% No matched command.\n");
		goto out;
	}

	if (num) {
		int i, width = 0;
		size_t len;

		for (i = 0; i < num; i++) {
			len = strlen(keys[i]);
			if (width < len)
				width = len;
		}

		for (i = 0; i < num; i++) {
			stream_puts(term->out, "  %-*s  %s\r\n", width, keys[i], descs[i]);
		}
	}

	if (cr) {
		stream_puts(term->out, "  %s\r\n", "<cr>");
	}

out:
	cmd_describe_free(ret, keys, descs);
	term_prompt(term);
	term_redraw_line(term);
}

static void term_beginning_of_line(struct term *term)
{
	while (term->in->cp)
		term_backward_char(term);
}

static void term_end_of_line(struct term *term)
{
	while (term->in->cp < term->in->len)
		term_forward_char(term);
}

static void term_kill_line(struct term *term)
{
	int i;
	size_t len;

	len = term->in->len - term->in->cp;
	if (len == 0)
		return;

	for (i = 0; i < len; i++)
		stream_putc(term->out, ' ');
	for (i = 0; i < len; i++)
		stream_putc(term->out, '\b');

	memset(&term->in->buf[term->in->cp], 0, len);
	term->in->len = term->in->cp;
}

static void term_kill_line_from_beginning(struct term *term)
{
	term_beginning_of_line(term);
	term_kill_line(term);
}

static void term_backward_word(struct term *term)
{
	struct buffer *in = term->in;

	while (in->cp > 0 && in->buf[in->cp - 1] == ' ') {
		term_backward_char(term);
	}

	while (in->cp > 0 && in->buf[in->cp - 1] != ' ') {
		term_backward_char(term);
	}
}

static void term_forward_word(struct term *term)
{
	struct buffer *in = term->in;

	while (in->cp < in->len && in->buf[in->cp] == ' ')
		term_forward_char(term);

	while (in->cp < in->len && in->buf[in->cp] != ' ')
		term_forward_char(term);
}

static void term_backward_kill_word(struct term *term)
{
	struct buffer *in = term->in;

	while (in->cp > 0 && in->buf[in->cp - 1] == ' ')
		term_delete_backward_char(term);

	while (in->cp > 0 && in->buf[in->cp - 1] != ' ')
		term_delete_backward_char(term);
}

static void term_forward_kill_word(struct term *term)
{
	struct buffer *in = term->in;

	while (in->cp < in->len && in->buf[in->cp] == ' ')
		term_delete_char(term);

	while (in->cp < in->len && in->buf[in->cp] != ' ')
		term_delete_char(term);
}

static void term_history_print(struct term *term, const char *line)
{
	int len;

	term_kill_line_from_beginning(term);

	len = strlen(line);
	memcpy(term->in->buf, line, len);
	term->in->cp = term->in->len = len;
	term->in->buf[term->in->len] = '\0';

	term_redraw_line(term);
}

static void term_next_line(struct term *term)
{
	const char *line;

	line = history_next(term->hist);
	if (line == NULL)
		return;

	term_history_print(term, line);
}

static void term_previous_line(struct term *term)
{
	const char *line;

	line = history_previous(term->hist);
	if (line == NULL)
		return;

	term_history_print(term, line);
}

static void term_read(struct term *term, int c)
{
	if (term->escape == TERM_ESCAPE) {
		if (c == 'A') {
			term_previous_line(term);
		} else if (c == 'B') {
			term_next_line(term);
		} else if (c == 'C') {
			term_forward_char(term);
		} else if (c == 'D') {
			term_backward_char(term);
		}

		term->escape = TERM_NORMAL;
		return;
	} else if (term->escape == TERM_PRE_ESCAPE) {
		if (c == 'b') {
			term_backward_word(term);
			term->escape = TERM_NORMAL;
		} else if (c == 'f') {
			term_forward_word(term);
			term->escape = TERM_NORMAL;
		} else if (c == 'd') {
			term_forward_kill_word(term);
			term->escape = TERM_NORMAL;
		} else if (c == CTRL('H') || c == CTRL_DEL) {
			term_backward_kill_word(term);
			term->escape = TERM_NORMAL;
		} else if (c == '[') {
			term->escape = TERM_ESCAPE;
		} else {
			term->escape = TERM_NORMAL;
		}
		return;
	} else {
		if (c == CTRL('B'))
			term_backward_char(term);
		else if (c == CTRL('F'))
			term_forward_char(term);
		else if (c == CTRL('H') || c == CTRL_DEL)
			term_delete_backward_char(term);
		else if (c == CTRL('D'))
			term_delete_char(term);
		else if (c == CTRL('A'))
			term_beginning_of_line(term);
		else if (c == CTRL('E'))
			term_end_of_line(term);
		else if (c == CTRL('N'))
			term_next_line(term);
		else if (c == CTRL('P'))
			term_previous_line(term);
		else if (c == CTRL('U'))
			term_kill_line_from_beginning(term);
		else if (c == CTRL('K'))
			term_kill_line(term);
		else if (c == CTRL('W'))
			term_forward_kill_word(term);
		else if (c == ESCAPE)
			term->escape = TERM_PRE_ESCAPE;
		else if (c == '?')
			term_describe_command(term);
		else if (c == '\t')
			term_complete_command(term);
		else if (c == '\n' || c == '\r')
			term_execute(term);
		else if (c > 31 && c < 127)
			term_self_insert(term, c);
	}

}
