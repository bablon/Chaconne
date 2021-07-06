#ifndef _TEST_H_
#define _TEST_H_

struct test {
	const char *name;
	const char *group;
	int (*run)(void *attr);
	void *attr;
} __attribute__ ((aligned (16)));

#define TEST(name, group, func, pattr)					\
	static int func(void *attr);					\
									\
	const struct test test##name					\
		 __attribute__ ((used, section ("test_section"))) = {	\
		#name, #group, func, pattr				\
	};								\
									\
	static int func(void *attr)

#endif
