#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "cli-term.h"

void print_args(struct term *term, int argc, char **argv)
{
	int i;

	term_print(term, "  argc %d.\r\n", argc);
	for (i = 0; i < argc; i++)
		term_print(term, "    arg%d, value %s.\r\n", i, argv[i]);
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

	if (argc == 0)
		return 0;
	else {
		int i, l = 0;

		for (i = 0; i < argc; i++) {
			if (i == 0)
				l += sprintf(buf + l, "%s", argv[i]);
			else
				l += sprintf(buf + l, " %s", argv[i]);
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
