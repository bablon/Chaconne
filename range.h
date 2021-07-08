#ifndef _RANGE_H_
#define _RANGE_H_

#include <stddef.h>

struct range {
	int start;
	int end;
	const char *desc;
	size_t desc_len;
};

int range_parse(const char *desc, struct range *range, size_t size);

#endif
