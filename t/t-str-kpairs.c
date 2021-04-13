#include <assert.h>
#include <string.h>
#include <stdio.h>

#include <str-kpairs.h>
#include "test-runner.h"

TEST(t_str_kpairs_init) {
	struct str_kpairs kps;

	str_kpairs_init(&kps);
	assert(sizeof(kps.buf) == STR_KPAIRS_BUFLEN);
	assert(kps.buf[0] == 0);
	assert(kps.last == kps.buf);
	assert(kps.end == kps.buf + sizeof(kps.buf));
}

TEST(t_str_kpairs_update) {
	struct str_kpairs kps;

	str_kpairs_init(&kps);
	assert(str_kpairs_get(&kps, "key") == NULL);
	assert(str_kpairs_set(&kps, "key", "value") == 0);
	assert(strcmp(str_kpairs_get(&kps, "key"), "value") == 0);
	assert(kps.last - kps.buf == sizeof("key") + sizeof("value"));

	str_kpairs_delete(&kps, "key");

	assert(kps.buf[0] == 0);
	assert(kps.last == kps.buf);
	assert(kps.end == kps.buf + sizeof(kps.buf));

	assert(str_kpairs_get(&kps, "key") == NULL);
	assert(str_kpairs_set(&kps, "key", "value") == 0);
	assert(strcmp(str_kpairs_get(&kps, "key"), "value") == 0);
	assert(kps.last - kps.buf == sizeof("key") + sizeof("value"));

	assert(str_kpairs_get(&kps, "dummy") == NULL);
	assert(str_kpairs_set(&kps, "key", "val") == 0);
	assert(strcmp(str_kpairs_get(&kps, "key"), "val") == 0);
	assert(kps.last - kps.buf == sizeof("key") + sizeof("val"));

	assert(str_kpairs_get(&kps, "dummy") == NULL);

	assert(str_kpairs_set(&kps, "key", "value") == 0);
	assert(strcmp(str_kpairs_get(&kps, "key"), "value") == 0);
	assert(kps.last - kps.buf == sizeof("key") + sizeof("value"));

	assert(str_kpairs_set(&kps, "k2", "v2") == 0);
	assert(strcmp(str_kpairs_get(&kps, "k2"), "v2") == 0);
}
