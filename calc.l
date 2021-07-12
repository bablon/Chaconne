%{
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "calc.h"
#include "calc_y.h"

void yyerror(char *s);
%}

%%

[0-9]+	{
	yylval.ival = strtol(yytext, NULL, 10);
	if (errno == ERANGE)
		yyerror("integer out of range");
	yylval.type = INTEGER;
	return NUMBER;
}

[0-9]+\.[0-9]+ {
	yylval.fval = strtod(yytext, NULL);
	if (errno == ERANGE)
		yyerror("double out of range");
	yylval.type = DOUBLE;
	return NUMBER;
}

[-+*/()\n] return *yytext;

[ \t]	;

.	yyerror("invalid character.");

%%

int yywrap(void)
{
	return 1;
}

/* yyparse(): input from string */
/* https://lists.gnu.org/archive/html/help-bison/2006-01/msg00054.html */

void calc_exp(char *str)
{
	char *s = str;
	size_t len = strlen(str);
	YY_BUFFER_STATE buf;

	if (str[len-1] != '\n') {
		s = malloc(len+2);
		memcpy(s, str, len);
		s[len] = '\n';
		s[len+1] = 0;
	}

	buf = yy_scan_string(s);
	yyparse();
	yy_delete_buffer(buf);
	if (s != str)
		free(s);
}
