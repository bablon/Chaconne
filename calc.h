#ifndef _CALC_H_
#define _CALC_H_

struct number {
	union {
		long ival;
		double fval;
	};
	int type;
};

#define YYSTYPE struct number
extern void calc_exp(char *str);

#endif
