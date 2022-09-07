%{
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "calc.h"
#include "calc_y.h"

void yyerror(void *scaner, char *s);
%}

%option reentrant noyywrap

%%

[0-9]+	{
	yylval.ival = strtol(yytext, NULL, 10);
	yylval.type = INTEGER;
	return NUMBER;
}

[0-9]+\.[0-9]+ {
	yylval.fval = strtod(yytext, NULL);
	yylval.type = DOUBLE;
	return NUMBER;
}

[-+*/()\n] return *yytext;

[ \t]	;

.

%%

void calc_exp(char *str)
{
	FILE *file;
	char *buf = NULL;
	size_t buf_size = 0;
	yyscan_t scaner;

	file = open_memstream(&buf, &buf_size);
	if (!file)
		return;

	fprintf(file, "%s\n", str);

	yylex_init(&scaner);
	yyset_in(file, scaner);
	yyparse(scaner);
	yylex_destroy(scaner);

	fclose(file);
	free(buf);
}
