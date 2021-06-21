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

#ifndef __CLI_TERM_H__
#define __CLI_TERM_H__

#define CMD_SUCCESS              0
#define CMD_WARNING              1
#define CMD_ERR_NO_MATCH         2
#define CMD_ERR_AMBIGUOUS        3
#define CMD_ERR_INCOMPLETE       4
#define CMD_ERR_EXEED_ARGC_MAX   5
#define CMD_ERR_NOTHING_TODO     6
#define CMD_COMPLETE_FULL_MATCH  7
#define CMD_COMPLETE_MATCH       8
#define CMD_COMPLETE_LIST_MATCH  9
#define CMD_SUCCESS_DAEMON      10
#define CMD_ERR_SYSTEM		11

struct term;
struct stream;

#define MAXARGC	64

struct cmdopt {
	char *argv[MAXARGC];
	int argc;
	struct hashtable *kpairs;
};

struct cmd_elem {
	const char *line;
	const char *desc;
	int (*func)(struct term *term, struct cmdopt *opt);
} __attribute__((aligned(16)));

struct cmdopt *cmdopt_create(void);
void cmdopt_clear(struct cmdopt *opt);
void cmdopt_destroy(struct cmdopt *opt);

#define COMMAND(func, line, desc)					\
	static int func(struct term *term, struct cmdopt *opt);		\
									\
	struct cmd_elem cmd_sec_##func					\
		__attribute__ ((used, section("cmd_section"))) = {	\
		line, desc, func					\
	};								\
									\
	static int func(struct term *term, struct cmdopt *opt)

struct event_loop;

struct cmd_node *cmd_tree_build(const struct cmd_elem *start, const struct cmd_elem *end);
struct cmd_node *cmd_tree_build_default(void);
void cmd_tree_delete(struct cmd_node *tree);
int cmd_execute(struct term *term, struct cmd_node *tree, const char *line);

void cmd_list_elems(struct stream *out);
void cmd_tree_show(struct stream *stream, struct cmd_node *tree);

int cmd_complete(struct cmd_node *tree, const char *line, int *n, char ***keys);
void cmd_complete_free(int ret, char **keys);
int cmd_describe(struct cmd_node *tree, const char *line, int *n,
		 char ***keys, char ***descs, int *cr);
void cmd_describe_free(int ret, char **keys, char **descs);
void cmd_tree_travel(struct cmd_node *tree, struct stream *out);

int term_fd(struct term *term);
struct term *term_create(struct event_loop *loop, int fd, const char *name);
void term_destroy(struct term *term);
void term_run(struct term *term);
int term_want_exit(struct term *term);

struct cmdopt *term_cmdopt(struct term *term);
struct stream *term_ostream(struct term *term);
struct cmd_node *term_cmd_tree(struct term *term);

void term_quit(struct term *term);
int term_print(struct term *term, const char *fmt, ...);
int term_flush(struct term *term);
void term_show_history(struct term *term);

#endif
