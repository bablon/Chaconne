#ifndef _CPUID_H_
#define _CPUID_H_

#include <stdint.h>

struct cpuidreg {
	uint32_t eax, ebx, ecx, edx;
};

static inline void cpuid(uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
	asm volatile ("cpuid"
		      : "+a" (*eax), "=b" (*ebx), "=c" (*ecx), "=d" (*edx));
}

static inline void do_cpuid(struct cpuidreg *id, uint32_t eax)
{
	id->eax = eax;
	id->ebx = 0;
	id->ecx = 0;
	id->edx = 0;

	cpuid(&id->eax, &id->ebx, &id->ecx, &id->edx);
}

#endif
