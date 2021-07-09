#include <assert.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "cpuid.h"

enum status {
	start,

	cpuid_name,
	cpuid_arg0,
	cpuid_arg1,

	brace_open,
	reg_name,
	reg_assign,
	reg_value,
	brace_close,

	done,
};

int cpuid_info_parse(const char *src, struct cpuid_info *info)
{
	const char *p = src;
	enum status status = start;
	const char *s = NULL;
	char *endptr = NULL;
	uint32_t eax = 0, ecx = 0, idx = 0;
	int i = 0, r;

	info->count = 0;
	for (i = 0; i < 32; i++) {
		memset(info->arr[i].count, 0, 16);
	}

	i = 0;

	for (;;) {
		switch (*p) {
		case '\0':
			if (status != start && status != brace_close) {
				printf("incomplete syntax\n");
				return -1;
			}
			status = done;
			break;
		case '{':
			if (status != cpuid_arg0 && status != cpuid_arg1) {
				printf("unexpected {\n");
				return -1;
			}
			s = NULL;
			status = brace_open;
			p++;
			break;
		case '}':
			if (status != brace_open && status != reg_value) {
				printf("unexpected }\n");
				return -1;
			}
			s = NULL;
			i++;
			status = brace_close;
			p++;
			break;
		case '=':
			if (status != reg_name) {
				printf("unexpected =\n");
				return -1;
			}
			status = reg_assign;
			endptr = NULL;
			r = range_parse(p+1, info->arr[i].rs[idx], 32, &endptr);
			if (r <= 0 || *endptr != '\n') {
				printf("invalid reg value\n");
				return -1;
			}
			info->arr[i].count[idx] = r;

			status = reg_value;

			s = NULL;
			p = endptr+1;

			break;
		default:
			if (!isspace(*p)) {
				if (!s) {
					s = p;
					if (status == cpuid_arg1) {
						printf("invalid %c, expect {", *p);
						return -1;
					}
				}
				p++;
				break;
			}

			if (s) {
				if (status == start || status == brace_close) {
					if (strncmp(s, "cpuid", 5)) {
						printf("unexpected %.*s, expected cpuid\n", (int)(p-s+1), s);
						return -1;
					}

					status = cpuid_name;
				} else if (status == cpuid_name) {
					eax = strtoul(s, &endptr, 0);
					if (endptr != p) {
						printf("invalid eax argument %.*s\n", (int)(p-s+1), s);
						return -1;
					}
					info->arr[i].eax = eax;
					status = cpuid_arg0;
				} else if (status == cpuid_arg0) {
					ecx = strtoul(s, &endptr, 0);
					if (endptr != p) {
						printf("invalid ecx argument %.*s\n", (int)(p-s+1), s);
					}
					info->arr[i].ecx = ecx;
					status = cpuid_arg1;
				} else if (status == brace_open || status == reg_value) {
					if (p-s != 3 || s[0] != 'e' || s[2] != 'x' ||
					    (s[1] != 'a' && s[1] != 'b' && s[1] != 'c' && s[1] != 'd')) {
						printf("invalid reg %.*s, expect e[a-d]x\n", (int)(p-s+1), s);
						return -1;
					}
					idx = s[1] - 'a';
					status = reg_name;
				} else {
					printf("invalid token %.*s\n", (int)(p-s+1), s);
					return -1;
				}

				s = NULL;
			}

			p++;
			break;
		}

		if (status == done)
			break;
	}

	info->count = i;

	return i;
}

void cpuid_info_dump(struct cpuid_info *info)
{
	int i, j;
	for (i = 0; i < info->count; i++) {
		printf("cpuid %d %d\r\n", info->arr[i].eax, info->arr[i].ecx);
		for (j = 0; j < 4; j++) {
			int k;
			struct range *rs = info->arr[i].rs[j];

			if (info->arr[i].count[j] > 0)
				printf("    e%cx:\n", 'a'+j);
			for (k = 0; k < info->arr[i].count[j]; k++) {
				printf("\t%d %d %.*s\n",
					rs[k].start, rs[k].end, (int)rs[k].desc_len, rs[k].desc);
			}
		}
	}
}

#if 0
int main(void)
{
	struct cpuid_info info;

	cpuid_info_parse((char *)_cpuid_desc, &info);
	cpuid_info_dump(&info);

	return 0;
}
#endif
