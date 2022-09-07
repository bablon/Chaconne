%{
#include <stdio.h>
#include "calc.h"

int yylex(void *scaner);
void yyerror(void *scaner, char *s);

static struct number calc(int op, int opsize, struct number a, struct number b);
%}

%token NUMBER INTEGER DOUBLE

%left '+' '-'
%left '*' '/'
%nonassoc UMINUS

%lex-param { void *scaner }
%parse-param { void *scaner }

%%

program :
	program expr '\n' {
		struct number num = $2;
		if (num.type == INTEGER)
			printf("%ld\n", num.ival);
		else
			printf("%lf\n", num.fval);
	}
	| program '\n'
	|
	;

expr :
	NUMBER		{ $$ = $1; }
	| expr '+' expr	{ $$ = calc('+', 2, $1, $3); }
	| expr '-' expr	{ $$ = calc('-', 2, $1, $3); }
	| expr '*' expr	{ $$ = calc('*', 2, $1, $3); }
	| expr '/' expr	{ $$ = calc('/', 2, $1, $3); }
	| '(' expr ')'	{ $$ = $2; }
	| '-' expr %prec UMINUS { $$ = calc('-', 1, $2, $2); }
	;

%%

void yyerror(void *scaner, char *s)
{
	fprintf(stderr, "%s\n", s);
}

struct number calc(int op, int opsize, struct number a, struct number b)
{
	struct number num;

	if (op == '-' && opsize == 1) {
		num.type = a.type;
		if (a.type == INTEGER)
			num.ival = -a.ival;
		else
			num.fval = -a.fval;
		return num;
	}

	switch (op) {
	case '+':
		if (a.type == INTEGER && b.type == INTEGER) {
			num.type = INTEGER;
			num.ival = a.ival + b.ival;
		} else {
			num.type = DOUBLE;
			num.fval = a.type == DOUBLE ? a.fval : a.ival;
			num.fval += b.type == DOUBLE ? b.fval : b.ival;
		}
		break;
	case '-':
		if (a.type == INTEGER && b.type == INTEGER) {
			num.type = INTEGER;
			num.ival = a.ival - b.ival;
		} else {
			num.type = DOUBLE;
			num.fval = a.type == DOUBLE ? a.fval : a.ival;
			num.fval -= b.type == DOUBLE ? b.fval : b.ival;
		}
		break;
	case '*':
		if (a.type == INTEGER && b.type == INTEGER) {
			num.type = INTEGER;
			num.ival = a.ival * b.ival;
		} else {
			num.type = DOUBLE;
			num.fval = a.type == DOUBLE ? a.fval : a.ival;
			num.fval *= b.type == DOUBLE ? b.fval : b.ival;
		}
		break;
	case '/':
		if (a.type == INTEGER && b.type == INTEGER) {
			num.type = INTEGER;
			num.ival = a.ival / b.ival;
		} else {
			num.type = DOUBLE;
			num.fval = a.type == DOUBLE ? a.fval : a.ival;
			num.fval /= b.type == DOUBLE ? b.fval : b.ival;
		}
		break;
	default:
		num.type = INTEGER;
		num.ival = 0;
		break;
	}

	return num;
}
