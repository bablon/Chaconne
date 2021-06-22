#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include "cli-term.h"
#include "hashtable.h"

static void kpair_dump(const void *key, const void *value, void *data)
{
	struct term *term = data;

	term_print(term, "  %s: %s.\r\n", (const char *)key, (const char *)value);
}

void print_args(struct term *term, struct cmdopt *opt)
{
	int i;
	size_t count = hashtable_count(opt->kpairs);

	if (opt->argc) {
		term_print(term, "argc %d\r\n", opt->argc);
		for (i = 0; i < opt->argc; i++)
			term_print(term, "  %d: %s.\r\n", i, opt->argv[i]);
	}

	if (count) {
		term_print(term, "keywords %zu\r\n", count);
		hashtable_travel(opt->kpairs, kpair_dump, term);
	}
}

#define SHOW_STR	"Show running system information\n"

COMMAND(show_history, NULL,
	"show history",
	"Show running system information\n"
	"Display ther command history\n")
{
	term_show_history(term);

	return 0;
}

COMMAND(show_cmdtree, NULL,
	"show cmdtree",
	SHOW_STR
	"Dump command tree (for debug)\n")
{
	cmd_tree_travel(term_cmd_tree(term), term_ostream(term));
	return 0;
}

COMMAND(cmd_system, NULL,
	"system .ARGS",
	"system shell\n"
	"command argument list\n")
{
	char buf[1024];

	if (opt->argc == 0)
		return 0;
	else {
		int i, l = 0;

		for (i = 0; i < opt->argc; i++) {
			if (i == 0)
				l += sprintf(buf + l, "%s", opt->argv[i]);
			else
				l += sprintf(buf + l, " %s", opt->argv[i]);
		}

		i = system(buf);
		if (i == -1) {
			term_print(term, "system error: %s\r\n", strerror(errno));
			return CMD_ERR_SYSTEM;
		} else if (WIFSIGNALED(i)) {
			term_print(term, "killed by signal %s\r\n:", WTERMSIG(i));
			return CMD_ERR_SYSTEM;
		}

		return 0;
	}
}

struct keywordopt {
	const char *subcmd;
	int number;
	bool eleven;
};

static struct keywordopt keywordopt;

static void keywordopt_init(void *buf, size_t size)
{
	struct keywordopt *k = buf;

	k->subcmd = NULL;
	k->number = -1;
	k->eleven = false;
}

int set_strptr(const char *src, void *buf)
{
	const char **pp = buf;

	*pp = src;
	return 0;
}

int set_i32(const char *src, void *buf)
{
	*(int *)buf = strtol(src, NULL, 10);
	return 0;
}

int set_bool(const char *src, void *buf)
{
	*(bool *)buf = true;
	return 0;
}

static struct optattr keyword_attrs[] = {
	{
		.index = 0,
		.key = NULL,
		.offset = offsetof(struct keywordopt, subcmd),
		.set = set_strptr,
	},
	{
		.index = -1,
		.key = "third",
		.offset = offsetof(struct keywordopt, number),
		.set = set_i32,
	},
	{
		.index = -1,
		.key = "eleven",
		.offset = offsetof(struct keywordopt, eleven),
		.set = set_bool,
	},
};

static struct cmdoptattr keyword_optattr = {
	.attrs = keyword_attrs,
	.size = sizeof(keyword_attrs)/sizeof(keyword_attrs[0]),
	.buf = &keywordopt,
	.bufsize = sizeof(struct keywordopt),
	.init = keywordopt_init,
};

COMMAND(cmd_keyword, &keyword_optattr,
	"keyword (t1|t2) {first|second|third INT} stage {ten|eleven|twelve}",
	"keyword example\n"
	"branch 1\n"
	"branch 2\n"
	"first\n"
	"second\n"
	"third\n"
	"FILE\n"
	"stage\n"
	"ten\n"
	"eleven\n"
	"twelve\n"
	)
{
	print_args(term, opt);
	term_print(term, "subcmd %s.\r\n", keywordopt.subcmd);
	term_print(term, "number %d.\r\n", keywordopt.number);
	term_print(term, "eleven %d.\r\n", keywordopt.eleven);

	return 0;
}
