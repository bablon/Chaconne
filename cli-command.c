#include <stdio.h>
#include <stdlib.h>
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

COMMAND(show_history,
	"show history",
	"Show running system information\n"
	"Display ther command history\n")
{
	term_show_history(term);

	return 0;
}

COMMAND(show_cmdtree,
	"show cmdtree",
	SHOW_STR
	"Dump command tree (for debug)\n")
{
	cmd_tree_travel(term_cmd_tree(term), term_ostream(term));
	return 0;
}

COMMAND(cmd_system,
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

COMMAND(cmd_keyword,
	"keyword (t1|t2) {first|second|third FILE} stage {ten|eleven|twelve}",
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
	return 0;
}
