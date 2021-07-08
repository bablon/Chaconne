#if defined(__i386__) || defined(__x86_64__)

#include <stdlib.h>

#include "cpuid.h"
#include "cli-term.h"
#include "hashtable.h"
#include "range.h"

COMMAND(cmd_cpuid, NULL,
	"cpuid {-eax UINT|-ecx UINT}",
	"cpuid command\n"
	"eax option for cpuid\n"
	"specify eax value\n"
	"ecx option for cpuid\n"
	"specify ecx value\n"
	)
{
	struct cpuidreg id;
	const char *eaxstr = hashtable_get(opt->kpairs, "-eax");
	const char *ecxstr = hashtable_get(opt->kpairs, "-ecx");
	uint32_t eax, ecx = 0;

	if (!eaxstr) {
		do_cpuid(&id, 0, 0);
		term_print(term, "%.*s%.*s%.*s, ",
			4, (char *)&id.ebx, 4, (char *)&id.edx, 4, (char *)&id.ecx);
		do_cpuid(&id, 0x80000002, 0);
		term_print(term, "%.*s%.*s%.*s%.*s", 4, (char *)&id.eax,
			4, (char *)&id.ebx, 4, (char *)&id.ecx, 4, (char *)&id.edx);
		do_cpuid(&id, 0x80000003, 0);
		term_print(term, "%.*s%.*s%.*s%.*s", 4, (char *)&id.eax,
			4, (char *)&id.ebx, 4, (char *)&id.ecx, 4, (char *)&id.edx);
		do_cpuid(&id, 0x80000004, 0);
		term_print(term, "%.*s%.*s%.*s%.*s.\r\n", 4, (char *)&id.eax,
			4, (char *)&id.ebx, 4, (char *)&id.ecx, 4, (char *)&id.edx);
		return 0;
	}

	eax = strtoul(eaxstr, NULL, 0);
	if (ecxstr)
		ecx = strtoul(ecxstr, NULL, 0);
	do_cpuid(&id, eax, ecx);

	if (eax == 0) {
		term_print(term, "eax 0x%x, ID %.*s%.*s%.*s.\r\n", id.eax,
			4, (char *)&id.ebx, 4, (char *)&id.edx, 4, (char *)&id.ecx);
	} else if (eax >= 0x80000002 && eax <= 0x80000004) {
		term_print(term, "Brand %.*s%.*s%.*s%.*s.\r\n", 4, (char *)&id.eax,
			4, (char *)&id.ebx, 4, (char *)&id.ecx, 4, (char *)&id.edx);
	} else {
		term_print(term, "eax 0x%x, ebx 0x%x, ecx 0x%x, edx 0x%x\r\n",
			id.eax, id.ebx, id.ecx, id.edx);

		/* parse ecx for cpu feature when calling cpuid -eax 1 */
		if (eax == 1) {
			const char *line =
				"0 0 fpu, 1 1 vme, 2 2 de, 3 3 pse, 4 4 tsc, 5 5 msr, 6 6 pae,"
				"7 7 mce, 8 8 cx8, 9 9 apic, 11 11 sep, 12 12 mtrr, 13 13 pge,"
				"14 14 mca, 15 15 cmov, 16 16 pat, 17 17 pse-36, 18 18 psn,"
				"19 19 clfsh, 21 21 ds, 22 22 acpi, 23 23 mmx, 24 24 fxsr,"
				"25 25 sse, 26 26 sse2, 27 27 ss, 28 28 htt, 29 29 tm, 30 30 ia64,"
				"31 31 pbe";
			struct range rs[32];
			int i, r;

			r = range_parse(line, rs, 32);
			if (r > 0)
				term_print(term, "edx:");
			for (i = 0; i < r; i++) {
				if (rs[i].start == rs[i].end) {
					if (id.edx & (1 << rs[i].start))
						term_print(term, " %.*s", rs[i].desc_len, rs[i].desc);
				}
			}
			if (r > 0)
				term_print(term, "\r\n");
		}
	}

	return 0;
}

#endif
