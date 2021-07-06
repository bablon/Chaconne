#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "test.h"
#include "hashtable.h"
#include "cli-term.h"

TEST(example0, example, example_test0, NULL)
{
	return 0;
}

TEST(example1, example, example_test1, NULL)
{
	return 1;
}

static int run_test(const struct test *t)
{
	int ret;

	ret = t->run(t->attr);

	exit(ret);
}

extern const struct test __start_test_section, __stop_test_section;

COMMAND(cmd_test, NULL,
	"test {-name TEST|-group GROUP}",
	"system test\n"
	"name option\n"
	"specify test name\n"
	"group option\n"
	"specify group name\n"
	)
{
	const struct test *t;
	pid_t pid;
	int total = 0, pass;
	siginfo_t info;
	const char *name = hashtable_get(opt->kpairs, "-name");
	const char *group = hashtable_get(opt->kpairs, "-group");

	pass = 0;
	for (t = &__start_test_section; t < &__stop_test_section; t++) {
		int success = 0;

		if (name && strcmp(name, t->name))
			continue;
		else if (group && strcmp(group, t->group))
			continue;

		total++;
		pid = fork();
		assert(pid >= 0);

		if (pid == 0)
			run_test(t); /* never returns */

		if (waitid(P_PID, pid, &info, WEXITED)) {
			fprintf(stderr, "waitid failed: %s\n", strerror(errno));
			abort();
		}

		switch (info.si_code) {
		case CLD_EXITED:
			if (info.si_status == EXIT_SUCCESS)
				success = 1;

			fprintf(stderr, "test \"%s\":\texit status %d",
				t->name, info.si_status);

			break;
		case CLD_KILLED:
		case CLD_DUMPED:
			fprintf(stderr, "test \"%s\":\tsignal %d",
				t->name, info.si_status);

			break;
		}

		if (success) {
			pass++;
			fprintf(stderr, ", pass.\n");
		} else
			fprintf(stderr, ", fail.\n");
	}

	fprintf(stderr, "----------------------------------------\n");
	fprintf(stderr, "%d tests, %d pass, %d fail\n",
		total, pass, total - pass);

	return pass == total ? EXIT_SUCCESS : EXIT_FAILURE;
}
