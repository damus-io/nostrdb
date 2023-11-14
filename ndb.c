

#include "nostrdb.h"
#include <stdio.h>

static int usage()
{
	printf("usage: ndb stat [db-dir]\n");
	return 1;
}

static inline void print_stat_counts(struct ndb_stat_counts *counts)
{
	printf("%zu\t%zu\t%zu\t%zu\n",
	       counts->count,
	       counts->key_size,
	       counts->value_size,
	       counts->key_size + counts->value_size);
}

static void print_stats(struct ndb_stat *stat)
{
	int i;
	const char *name;
	struct ndb_stat_counts *c;

	struct ndb_stat_counts total;
	ndb_stat_counts_init(&total);

	printf("name\tcount\tkey_bytes\tvalue_bytes\ttotal_bytes\n");
	printf("---\ndbs\n---\n");
	for (i = 0; i < NDB_DBS; i++) {
		name = ndb_db_name(i);

		total.count += stat->dbs[i].count;
		total.key_size += stat->dbs[i].key_size;
		total.value_size += stat->dbs[i].value_size;

		printf("%s\t", name);
		print_stat_counts(&stat->dbs[i]);
	}

	printf("total\t");
	print_stat_counts(&total);

	printf("-----\nkinds\n-----\n");
	for (i = 0; i < NDB_CKIND_COUNT; i++) {
		c = &stat->common_kinds[i];
		if (c->count == 0)
			continue;

		printf("%s\t", ndb_kind_name(i));
		print_stat_counts(c);
	}

	if (stat->other_kinds.count != 0) {
		printf("other\t");
		print_stat_counts(&stat->other_kinds);
	}
}

int main(int argc, char *argv[])
{
	struct ndb *ndb;
	int threads = 8;
	int flags = 0;
	struct ndb_stat stat;
	const char *dir;
	size_t mapsize = 1024ULL * 1024ULL * 1024ULL * 1024ULL; // 1 TiB

	if (argc < 2 && strcmp(argv[1], "stat")) {
		return usage();
	}

	dir = argv[2] == NULL ? "." : argv[2];

	if (!ndb_init(&ndb, dir, mapsize, threads, flags)) {
		return 2;
	}

	if (!ndb_stat(ndb, &stat)) {
		return 3;
	}

	print_stats(&stat);
}
