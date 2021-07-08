#if defined(__i386__) || defined(__x86_64__)

#include <stdlib.h>

#include "cpuid.h"
#include "cli-term.h"
#include "hashtable.h"

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
	}

	return 0;
}

#endif
