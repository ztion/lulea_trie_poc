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

#ifndef __LULEA_TRIE_H__
#define __LULEA_TRIE_H__

#include <stdint.h>
#include "routing_table_split.h"

typedef struct tagBUCKET
{
  PROUTEENTRY pPrefixes;
  uint32_t    u32NumPrefixes;
} BUCKET, *PBUCKET;

typedef struct tagCODEWORD
{
  //uint32_t u32Offset;      /* Number of pointers that existed before this codeword */
  //uint16_t u16Bitmask;     /* Bitmask, or if high bit is set, 15 bit offset into next hop array */
  uint64_t u64BitmaskOffset; /* High 32 bits: Bitmask, or if high bit is set, 63 bits of next hop pointer.
                                Low 32 bits:  Offset, number of pointers that existed before this codeword */

} CODEWORD, *PCODEWORD;

#define CODEWORD_NEXTHOP (1ULL << 63)

typedef struct tagLEVEL1
{
  CODEWORD codewords[4096];
  uint32_t au32Pointers[];
} LEVEL1, *PLEVEL1;

typedef struct tagLEVEL23
{
  CODEWORD codewords[16];
  uint32_t au32Pointers[];
} LEVEL23, *PLEVEL23;

#define POINTERTYPE_NEXTHOP (0)
#define POINTERTYPE_NEXTLEVEL (1U << 31)

typedef int (*BUILDCALLBACK)(uint32_t *pu32Pointer, PROUTEENTRY pPrefixes, char **ppchCurrentPos);

typedef struct tagBUILDTASK
{
  uint32_t      *pu32Pointer;
  PROUTEENTRY    pPrefixes;

  BUILDCALLBACK  fpBuild;

  struct tagBUILDTASK *pNext, *pPrev;

} BUILDTASK, *PBUILDTASK;

int BuildLuleaTrie(PTREENODE pTreeRoot, PROUTEENTRY pNextHops, unsigned int uNumPrefixes);
PROUTEENTRY LuleaTrieLookup(uint32_t u32IP, PROUTEENTRY pNextHops);

#endif /* __LULEA_TRIE_H__ */