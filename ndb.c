

#include "nostrdb.h"
#include <stdio.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

static int usage()
{
	printf("usage: ndb [--skip-verification] [-d db_dir] <command>\n\n");
	printf("commands\n\n");
	printf("	stat\n");
	printf("	search <fulltext query>\n");
	printf("	import <line-delimited json file>\n\n");
	printf("settings\n\n");
	printf("	--skip-verification  skip signature validation\n");
	printf("	-d <db_dir>          set database directory\n");
	return 1;
}


static int map_file(const char *filename, unsigned char **p, size_t *flen)
{
	struct stat st;
	int des;
	stat(filename, &st);
	*flen = st.st_size;

	des = open(filename, O_RDONLY);

	*p = mmap(NULL, *flen, PROT_READ, MAP_PRIVATE, des, 0);
	close(des);

	return *p != MAP_FAILED;
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
	int threads = 6;
	int i, flags;
	struct ndb_stat stat;
	struct ndb_txn txn;
	struct ndb_text_search_results results;
	const char *dir;
	unsigned char *data;
	size_t data_len;
	size_t mapsize = 1024ULL * 1024ULL * 1024ULL * 1024ULL; // 1 TiB

	if (argc < 2) {
		return usage();
	}

	dir = ".";
	flags = 0;
	for (i = 0; i < 2; i++)
	{
		if (!strcmp(argv[1], "-d") && argv[2]) {
			dir = argv[2];
			argv += 2;
			argc -= 2;
		} else if (!strcmp(argv[1], "--skip-verification")) {
			flags = NDB_FLAG_SKIP_NOTE_VERIFY;
			argv += 1;
			argc -= 1;
		}
	}

	fprintf(stderr, "using db '%s'\n", dir);

	if (!ndb_init(&ndb, dir, mapsize, threads, flags)) {
		return 2;
	}

	if (argc == 3 && !strcmp(argv[1], "search")) {
		ndb_begin_query(ndb, &txn);
		ndb_text_search(&txn, argv[2], &results);
		ndb_end_query(&txn);
	} else if (argc == 2 && !strcmp(argv[1], "stat")) {
		if (!ndb_stat(ndb, &stat)) {
			return 3;
		}

		print_stats(&stat);
	} else if (argc == 3 && !strcmp(argv[1], "import")) {
		map_file(argv[2], &data, &data_len);
		ndb_process_events(ndb, (const char *)data, data_len);
	} else {
		return usage();
	}

	ndb_destroy(ndb);
}
