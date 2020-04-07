/* An implementation of the Luleå algorithm, slightly modified.
 * Copyright (C) 2020 Kristoffer Brånemyr
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "bgpdump_lib.h"
#include "routing_table_split.h"
#include "linked_list.h"
#include "read_bgp.h"

static PREFIXES prefixes;

PPREFIXES ReadFromBgpDump(char *filename)
{
	BGPDUMP       *dumpfile = NULL;
	BGPDUMP_ENTRY *entry    = NULL;

	dumpfile = bgpdump_open_dump(filename);
	if (!dumpfile)
	{
		printf("Could not open file %s\n", filename);
		exit(1);
	}

	do
	{
		entry = bgpdump_read_next(dumpfile);
		if (entry)
		{
			if (entry->type == BGPDUMP_TYPE_TABLE_DUMP_V2)
			{
				char prefix[64];
				BGPDUMP_TABLE_DUMP_V2_PREFIX *prefix_entry;
				PROUTEENTRY pNewEntry = NULL;

				prefix_entry = &entry->body.mrtd_table_dump_v2_prefix;
				if (prefix_entry->afi == AFI_IP)
				{
					strcpy(prefix, inet_ntoa(prefix_entry->prefix.v4_addr));
					//printf("Prefix: %s/%d\n", prefix, prefix_entry->prefix_length);

					pNewEntry = calloc(1, sizeof(*pNewEntry));
					if (!pNewEntry)
					{
						printf("Out of memory!\n");
						exit(1);
					}

					pNewEntry->u32Start = ntohl(prefix_entry->prefix.v4_addr.s_addr);
					pNewEntry->u32NextHopIndex = NO_NEXT_HOP;
					if (prefix_entry->prefix_length == 0)
					{
						uint32_t u32IP = pNewEntry->u32Start;
						uint32_t u32Size = 1 << (32 - 1);

						pNewEntry->u32Size = u32Size;
						InsertIntoLinkedList(&prefixes.pPrefixes[prefix_entry->prefix_length], pNewEntry);

						prefixes.uNumPrefixes[prefix_entry->prefix_length]++;
						prefixes.uTotalPrefixes++;

						pNewEntry = calloc(1, sizeof(*pNewEntry));
						if (!pNewEntry)
						{
							printf("Out of memory!\n");
							exit(1);
						}
						pNewEntry->u32Size = u32Size;
						pNewEntry->u32Start = u32IP + u32Size;
						pNewEntry->u32NextHopIndex = NO_NEXT_HOP;

					}
					else
					{
						pNewEntry->u32Size = 1 << (32 - prefix_entry->prefix_length);
					}

					/* Insert prefixes into 33 different linked lists, for prefixes from /32 to /0.
					   Makes it easier to produce trie later. */
					InsertIntoLinkedList(&prefixes.pPrefixes[prefix_entry->prefix_length], pNewEntry);
					prefixes.uNumPrefixes[prefix_entry->prefix_length]++;
					prefixes.uTotalPrefixes++;
				}

			}
			bgpdump_free_mem(entry);
		}
	} while (!dumpfile->eof);

	bgpdump_close_dump(dumpfile);

	return &prefixes;
}
