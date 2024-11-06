#include "mospf_database.h"
#include "ip.h"

#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>

struct list_head mospf_db;

void init_mospf_db()
{
	init_list_head(&mospf_db);
}

void print_database(void)
{
	fprintf(stdout, "MOSPF Database:\n");
	fprintf(stdout, "RID\t\tNetwork\t\tMask\t\tNeighbor\n");
	fprintf(stdout, "--------------------------------------------------------------------------------\n");
	mospf_db_entry_t *db_entry = NULL;
	list_for_each_entry(db_entry, &mospf_db, list) {
		for (int i=0; i<db_entry->nadv; i++) {
			fprintf(stdout, IP_FMT"\t"IP_FMT"\t"IP_FMT"\t"IP_FMT"\t\n",
				HOST_IP_FMT_STR(db_entry->rid),
				HOST_IP_FMT_STR(db_entry->array[i].network),
				HOST_IP_FMT_STR(db_entry->array[i].mask),
				HOST_IP_FMT_STR(db_entry->array[i].rid));
		}
		fprintf(stdout, "\n");
	}
	fprintf(stdout, "-------------------------------------------------------------------------------\n");
}

