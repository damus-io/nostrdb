

#include "nostrdb.h"
#include "lmdb.h"
#include "print_util.h"
#include "hex.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

static int usage()
{
	printf("usage: ndb [--skip-verification] [-d db_dir] <command>\n\n");

	printf("commands\n\n");

	printf("	stat\n");
	printf("	query [--kind 42] [--id abcdef...] [--notekey key] [--search term] [--limit 42] \n");
	printf("	      [-e abcdef...] [--author abcdef... -a bcdef...] [--relay wss://relay.damus.io]\n");
	printf("	profile <pubkey>                            print the raw profile data for a pubkey\n");
	printf("	note-relays <note-id>                       list the relays a given note id has been seen on\n");
	printf("	print-search-keys\n");
	printf("	print-kind-keys\n");
	printf("	print-tag-keys\n");
	printf("	print-relay-kind-index-keys\n");
	printf("	print-author-kind-index-keys\n");
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

int ndb_print_search_keys(struct ndb_txn *txn);
int ndb_print_kind_keys(struct ndb_txn *txn);
int ndb_print_tag_index(struct ndb_txn *txn);
int ndb_print_relay_kind_index(struct ndb_txn *txn);
int ndb_print_author_kind_index(struct ndb_txn *txn);

static void print_note(struct ndb_note *note)
{
	static char buf[5000000];
	if (!ndb_note_json(note, buf, sizeof(buf))) {
		print_hex_stream(stderr, ndb_note_id(note), 32);
		fprintf(stderr, " is too big to print! >5mb");
		return;
	}
	puts(buf);
}

static void ndb_print_text_search_result(struct ndb_txn *txn,
		struct ndb_text_search_result *r)
{
	size_t len;
	struct ndb_note *note;

	//ndb_print_text_search_key(&r->key);

	if (!(note = ndb_get_note_by_key(txn, r->key.note_id, &len))) {
		fprintf(stderr,": note not found");
		return;
	}

	//fprintf(stderr,"\n");
	print_note(note);
}


int main(int argc, char *argv[])
{
	struct ndb *ndb;
	int i, flags, limit, count, current_field, len, res;
	long nanos;
	struct ndb_stat stat;
	struct ndb_txn txn;
	const char *dir;
	unsigned char *data;
	size_t data_len;
	struct ndb_config config;
	struct timespec t1, t2;
	unsigned char tmp_id[32];
	char buf[1024];
	buf[0] = 0;

	// profiles
	const char *arg_str;
	void *ptr;
	size_t profile_len;
	uint64_t key;

	res = 0;
	ndb_default_config(&config);
	ndb_config_set_mapsize(&config, 1024ULL * 1024ULL * 1024ULL * 1024ULL /* 1 TiB */);

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

	ndb_config_set_flags(&config, flags);

	//fprintf(stderr, "using db '%s'\n", dir);

	if (!ndb_init(&ndb, dir, &config)) {
		return 2;
	}

	if (argc == 2 && !strcmp(argv[1], "stat")) {
		if (!ndb_stat(ndb, &stat)) {
			res = 3;
			goto cleanup;
		}

		print_stats(&stat);
	} else if (argc >= 3 && !strcmp(argv[1], "query")) {
		struct ndb_filter filter, *f = &filter;
		ndb_filter_init(f);

		argv += 2;
		argc -= 2;
		current_field = 0;

		for (i = 0; argc && i < 1000; i++) {
			if (!strcmp(argv[0], "-k") || !strcmp(argv[0], "--kind")) {
				if (current_field != NDB_FILTER_KINDS) {
					ndb_filter_end_field(f);
					ndb_filter_start_field(f, NDB_FILTER_KINDS);
				}
				current_field = NDB_FILTER_KINDS;
				ndb_filter_add_int_element(f, atoll(argv[1]));
				argv += 2;
				argc -= 2;
			} else if (!strcmp(argv[0], "--notekey")) {
				key = atol(argv[1]);
				argv += 2;
				argc -= 2;
			} else if (!strcmp(argv[0], "-l") || !strcmp(argv[0], "--limit")) {
				limit = atol(argv[1]);
				if (current_field) {
					ndb_filter_end_field(f);
					current_field = 0;
				}
				ndb_filter_start_field(f, NDB_FILTER_LIMIT);
				current_field = NDB_FILTER_LIMIT;
				ndb_filter_add_int_element(f, limit);
				ndb_filter_end_field(f);
				argv += 2;
				argc -= 2;
			} else if (!strcmp(argv[0], "--since") || !strcmp(argv[0], "-s")) {
				if (current_field) {
					ndb_filter_end_field(f);
					current_field = 0;
				}
				ndb_filter_start_field(f, NDB_FILTER_SINCE);
				ndb_filter_add_int_element(f, atoll(argv[1]));
				ndb_filter_end_field(f);
				argv += 2;
				argc -= 2;
			} else if (!strcmp(argv[0], "--until") || !strcmp(argv[0], "-u")) {
				if (current_field) {
					ndb_filter_end_field(f);
					current_field = 0;
				}
				ndb_filter_start_field(f, NDB_FILTER_UNTIL);
				ndb_filter_add_int_element(f, atoll(argv[1]));
				ndb_filter_end_field(f);
				argv += 2;
				argc -= 2;
			} else if (!strcmp(argv[0], "-t")) {
				if (current_field) {
					ndb_filter_end_field(f);
					current_field = 0;
				}
				ndb_filter_start_tag_field(f, 't');
				ndb_filter_add_str_element(f, argv[1]);
				ndb_filter_end_field(f);
				argv += 2;
				argc -= 2;
			} else if (!strcmp(argv[0], "--search") || !strcmp(argv[0], "-S")) {
				if (current_field) {
					ndb_filter_end_field(f);
					current_field = 0;
				}
				ndb_filter_start_field(f, NDB_FILTER_SEARCH);
				ndb_filter_add_str_element(f, argv[1]);
				ndb_filter_end_field(f);
				argv += 2;
				argc -= 2;
			} else if (!strcmp(argv[0], "--relay") || !strcmp(argv[0], "-r")) {
				if (current_field) {
					ndb_filter_end_field(f);
					current_field = 0;
				}
				ndb_filter_start_field(f, NDB_FILTER_RELAYS);
				ndb_filter_add_str_element(f, argv[1]);
				ndb_filter_end_field(f);
				argv += 2;
				argc -= 2;
			} else if (!strcmp(argv[0], "-i") || !strcmp(argv[0], "--id")) {
				if (current_field != NDB_FILTER_IDS) {
					ndb_filter_end_field(f);
					ndb_filter_start_field(f, NDB_FILTER_IDS);
					current_field = NDB_FILTER_IDS;
				}

				len = strlen(argv[1]);
				if (len != 64 || !hex_decode(argv[1], 64, tmp_id, sizeof(tmp_id))) {
					fprintf(stderr, "invalid hex id\n");
					res = 42;
					goto cleanup;
				}

				ndb_filter_add_id_element(f, tmp_id);
				argv += 2;
				argc -= 2;
			} else if (!strcmp(argv[0], "-e")) {
				if (current_field != 'e') {
					if (!ndb_filter_start_tag_field(f, 'e')) {
						fprintf(stderr, "field already started\n");
						res = 44;
						goto cleanup;
					}
				}
				current_field = 'e';

				if (len != 64 || !hex_decode(argv[1], 64, tmp_id, sizeof(tmp_id))) {
					fprintf(stderr, "invalid hex id\n");
					res = 42;
					goto cleanup;
				}

				if (!ndb_filter_add_id_element(f, tmp_id)) {
					fprintf(stderr, "too many event ids\n");
					res = 43;
					goto cleanup;
				}

				argv += 2;
				argc -= 2;
			} else if (!strcmp(argv[0], "-a") || !strcmp(argv[0], "--author")) {
				if (current_field != NDB_FILTER_AUTHORS) {
					ndb_filter_end_field(f);
					ndb_filter_start_field(f, NDB_FILTER_AUTHORS);
					current_field = NDB_FILTER_AUTHORS;
				}

				len = strlen(argv[1]);
				if (len != 64 || !hex_decode(argv[1], 64, tmp_id, sizeof(tmp_id))) {
					fprintf(stderr, "invalid hex pubkey\n");
					res = 42;
					goto cleanup;
				}

				if (!ndb_filter_add_id_element(f, tmp_id)) {
					fprintf(stderr, "too many author pubkeys\n");
					res = 43;
					goto cleanup;
				}

				argv += 2;
				argc -= 2;
			} else {
				fprintf(stderr, "unrecognized option: %s\n", argv[0]);
				res = 100;
				goto cleanup;
			}
		}

		if (current_field) {
			ndb_filter_end_field(f);
			current_field = 0;
		}

		ndb_filter_end(f);

		ndb_filter_json(f, buf, sizeof(buf));
		fprintf(stderr, "using filter '%s'\n", buf);

		struct ndb_query_result results[10000];
		ndb_begin_query(ndb, &txn);


		clock_gettime(CLOCK_MONOTONIC, &t1);
		if (key) {
			results[0].note = ndb_get_note_by_key(&txn, key, NULL);
			if (results[0].note != NULL)
				count = 1;
			else
				count = 0;
		} else if (!ndb_query(&txn, f, 1, results, 10000, &count)) {
			fprintf(stderr, "query error\n");
		}
		clock_gettime(CLOCK_MONOTONIC, &t2);

		nanos = (t2.tv_sec - t1.tv_sec) * (long)1e9 + (t2.tv_nsec - t1.tv_nsec);

		fprintf(stderr, "%d results in %f ms\n", count, nanos/1000000.0);
		for (i = 0; i < count; i++) {
			print_note(results[i].note);
		}

		ndb_end_query(&txn);

	} else if (argc == 3 && !strcmp(argv[1], "import")) {
		if (!strcmp(argv[2], "-")) {
			ndb_process_events_stream(ndb, stdin);
		} else {
			map_file(argv[2], &data, &data_len);
			ndb_process_events(ndb, (const char *)data, data_len);
			//ndb_process_client_events(ndb, (const char *)data, data_len);
		}
	} else if (argc == 2 && !strcmp(argv[1], "print-search-keys")) {
		ndb_begin_query(ndb, &txn);
		ndb_print_search_keys(&txn);
		ndb_end_query(&txn);
	} else if (argc == 2 && !strcmp(argv[1], "print-kind-keys")) {
		ndb_begin_query(ndb, &txn);
		ndb_print_kind_keys(&txn);
		ndb_end_query(&txn);
	} else if (argc == 2 && !strcmp(argv[1], "print-tag-keys")) {
		ndb_begin_query(ndb, &txn);
		ndb_print_tag_index(&txn);
		ndb_end_query(&txn);
	} else if (argc == 2 && !strcmp(argv[1], "print-relay-kind-index-keys")) {
		ndb_begin_query(ndb, &txn);
		ndb_print_relay_kind_index(&txn);
		ndb_end_query(&txn);
	} else if (argc == 2 && !strcmp(argv[1], "print-author-kind-index-keys")) {
		ndb_begin_query(ndb, &txn);
		ndb_print_author_kind_index(&txn);
		ndb_end_query(&txn);
	} else if (argc == 3 && !strcmp(argv[1], "note-relays")) {
		struct ndb_note_relay_iterator iter;
		const char *relay;

		ndb_begin_query(ndb, &txn);
		arg_str = argv[2];

		if (!hex_decode(arg_str, strlen(arg_str), tmp_id, sizeof(tmp_id))) {
			fprintf(stderr, "failed to decode hex pubkey '%s'\n", arg_str);
			res = 88;
			goto cleanup;
		}

		if (!(key = ndb_get_notekey_by_id(&txn, tmp_id))) {
			fprintf(stderr, "noteid '%s' not found\n", arg_str);
			res = 89;
			goto cleanup;
		}

		ndb_note_relay_iterate_start(&txn, &iter, key);

		for (i = 0; (relay = ndb_note_relay_iterate_next(&iter)); i++) {
			printf("%s\n", relay);
		}

		fprintf(stderr, "seen on %d relays\n", i);

		ndb_end_query(&txn);
	} else if (argc == 3 && !strcmp(argv[1], "profile")) {
		arg_str = argv[2];
		if (!hex_decode(arg_str, strlen(arg_str), tmp_id, sizeof(tmp_id))) {
			fprintf(stderr, "failed to decode hex pubkey '%s'\n", arg_str);
			res = 88;
			goto cleanup;
		}
		ndb_begin_query(ndb, &txn);
		if (!(ptr = ndb_get_profile_by_pubkey(&txn, tmp_id, &profile_len, &key))) {
			ndb_end_query(&txn);
			fprintf(stderr, "profile not found\n");
			res = 89;
			goto cleanup;
		}
		ndb_end_query(&txn);
		print_hex(ptr, profile_len);
		printf("\n");
	} else {
		ndb_destroy(ndb);
		return usage();
	}

cleanup:
	ndb_destroy(ndb);
	return res;
}
