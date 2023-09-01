#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nostrdb.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

__AFL_FUZZ_INIT();

int main(void) {
    while (__AFL_LOOP(1000000)) {
        unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;
        size_t len = __AFL_FUZZ_TESTCASE_LEN;
        struct ndb *db;
        int init_result = ndb_init(&db, 1024, 1);
        if (init_result != 1) {
            fprintf(stderr, "Failed to initialize nostrdb\n");
            continue;
        }
        int result = ndb_process_event(db, (const char *)buf, len);
        if (result == 0) {
            if (len >= 32) {
                struct ndb_note *note = ndb_get_note_by_id(db, buf);
                if (note != NULL) {
                }
            }
        } else {
        }
        ndb_destroy(db);
    }
    return 0;
}
