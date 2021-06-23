# Chaconne

A terminal system with a command line engine, which makes it easy to add new commands.

Command BNF syntax inspired by Quagga

	command : literal
		| literal tokens
		;

	tokens : literal
		| option
		| variable
		| vararg
		;

	literal : [_-a-z0-9]+
		;

	option : '(' literal literals ')'
		;

	literals : '|' literal
		;

	variable : [A-Z]+
		;

	vararg : .[A-Z]+
		;

The following is a complex example in `cli-command.c` for command `keyword (t1|t2) {first|second|third INT} stage {ten|eleven|twelve}`. The parser will automatically fill the user-defined argument structure before executing the command.

	struct keywordopt {
		const char *subcmd;
		int number;
		bool eleven;
	};

	static struct keywordopt keywordopt;

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
		...
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
		"Integer number\n"
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
