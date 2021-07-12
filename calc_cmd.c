#include <stdio.h>

#include "cli-term.h"
#include "calc.h"

COMMAND(cmd_calc_exp, NULL,
	"calc .EXP",
	"calculator\n"
	"arithmetic expression\n")
{
	int i, l = 0;
	char buf[1024];

	if (opt->argc == 0) {
		term_print(term, "expect an expression\r\n");
		return 0;
	}

	for (i = 0; i < opt->argc; i++) {
		if (i == 0)
			l += sprintf(buf + l, "%s", opt->argv[i]);
		else
			l += sprintf(buf + l, " %s", opt->argv[i]);
	}

	calc_exp(buf);
	return 0;
}
