

#include "io.h"
#include "nostrdb.h"
#include <sys/mman.h>
#include <time.h>
#include <stdlib.h>
#include <assert.h>

static int bench_parser(int times)
{
	char *json = malloc(1024 * 100);
	int i, mapsize, written, ingester_threads;
	long nanos, ms;
	static const int alloc_size = 2 << 18;
	struct ndb *ndb;
	struct timespec t1, t2;

	written = 0;
	mapsize = 1024 * 1024 * 100;
	ingester_threads = 8;
	assert(ndb_init(&ndb, "testdata/db", mapsize, ingester_threads));
	read_file("testdata/contacts-event.json", (unsigned char*)json, alloc_size, &written);

	clock_gettime(CLOCK_MONOTONIC, &t1);

	for (i = 0; i < times; i++) {
		ndb_process_event(ndb, json, written);
	}

	free(json);
	ndb_destroy(ndb);

	clock_gettime(CLOCK_MONOTONIC, &t2);

	nanos = (t2.tv_sec - t1.tv_sec) * (long)1e9 + (t2.tv_nsec - t1.tv_nsec);
	ms = nanos / 1e6;
	printf("ns/run\t%ld\nms/run\t%f\nns\t%ld\nms\t%ld\n",
		nanos/times, (double)ms/(double)times, nanos, ms);

	return 1;
}

int main(int argc, char *argv[], char **env)
{
	int times = 50000;

	if (argc >= 2)
		times = atoi(argv[1]);

	fprintf(stderr, "benching %d duplicate contact events\n", times);
	if (!bench_parser(times))
		return 2;
	
	return 0;
}

