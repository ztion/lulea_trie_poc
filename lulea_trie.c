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
#include <stdint.h>
#include "lulea_trie.h"
#include "linked_list.h"
#include "queue.h"


static PBUCKET      pLevel1Buckets;
static char        *pachBucketGroupNumPrefixes;

static char        *pchLuleaTrie;

static PLEVEL1      pLevel1;

static PBUILDTASK   pBuildTaskHead;
static PBUILDTASK   pBuildTaskTail;

int ProcessBucketGroups(PBUCKET pBuckets, char *pchBucketGroupNumPrefixes, unsigned int uMaxIndex, PCODEWORD pCodewords, char **ppchCurrentLocation, BUILDCALLBACK fpBuildCallback);


int BucketPrefix(PBUCKET pBuckets, unsigned int uBucketValue, char *pachBucketGroupPrefixes, PROUTEENTRY pRouteEntry)
{
  InsertIntoLinkedList(&pBuckets[uBucketValue].pPrefixes, pRouteEntry);
  pBuckets[uBucketValue].u32NumPrefixes++;

  if (pBuckets[uBucketValue].u32NumPrefixes == 1)
  {
    /* Count how many of the 16 slots in the bucket group is occupied */
    pachBucketGroupPrefixes[uBucketValue / 16]++;
  }

  return 1;
}

int RecurseRadixTree(PTREENODE pTreeNode)
{
  if (pTreeNode->pLeft)
  {
    RecurseRadixTree(pTreeNode->pLeft);
  }
  if (pTreeNode->pRight)
  {
    RecurseRadixTree(pTreeNode->pRight);
  }

  if (pTreeNode->pRoute)
  {
    uint16_t u16Level1Offset = 0;

    u16Level1Offset = (pTreeNode->pRoute->u32Start & 0xFFFF0000) >> 16;

    BucketPrefix(pLevel1Buckets, u16Level1Offset, pachBucketGroupNumPrefixes, pTreeNode->pRoute);
  }

  return 1;
}

unsigned int FirstNextHopFromBucketGroup(PBUCKET pBuckets, unsigned int uStart)
{
  unsigned int uIndex = 0;

  for (uIndex = uStart; uIndex < uStart + 16; uIndex++)
  {
    PROUTEENTRY pRoute = pBuckets[uIndex].pPrefixes;
    if (pRoute)
    {
      return pRoute->u32NextHopIndex;
    }
  }

  return NO_NEXT_HOP;
}

int ProcessLevel23(uint32_t *pu32Pointer, PROUTEENTRY pPrefixes, char **ppchCurrentPos, unsigned int uShiftValue, BUILDCALLBACK fpBuildCallback)
{
  BUCKET       buckets[256]   = { 0 };
  char         achBucketGroupPrefixes[16] = { 0 };
  PROUTEENTRY  pProcessEntry  = NULL;
  PROUTEENTRY  pTmp           = NULL;
  unsigned int uBucketValue = 0;
  PLEVEL23     pLevel23       = (PLEVEL23)(*ppchCurrentPos);


  /* Set pointer from level above to point to this chunk */
  *pu32Pointer     = POINTERTYPE_NEXTLEVEL | (*ppchCurrentPos - pchLuleaTrie); 
  *ppchCurrentPos += sizeof(LEVEL23);

  pProcessEntry = pPrefixes;
  while (pProcessEntry)
  {
    pTmp = pProcessEntry->pNext;
    pProcessEntry->pPrev = NULL;
    pProcessEntry->pNext = NULL;

    uBucketValue = (pProcessEntry->u32Start >> uShiftValue) & 0xFF;

    BucketPrefix(buckets, uBucketValue, achBucketGroupPrefixes, pProcessEntry);

    pProcessEntry = pTmp;
  }

  return ProcessBucketGroups(buckets, achBucketGroupPrefixes, 16, pLevel23->codewords, ppchCurrentPos, fpBuildCallback);
}

int ProcessLevel3(uint32_t *pu32Pointer, PROUTEENTRY pPrefixes, char **ppchCurrentPos)
{
  return ProcessLevel23(pu32Pointer, pPrefixes, ppchCurrentPos, 0, NULL);
}

int ProcessLevel2(uint32_t *pu32Pointer, PROUTEENTRY pPrefixes, char **ppchCurrentPos)
{
  return ProcessLevel23(pu32Pointer, pPrefixes, ppchCurrentPos, 8, ProcessLevel3);
}

int ProcessMultiPrefixBucket(PBUCKET pBuckets, unsigned int uStartBucket, uint16_t *pu16Bitmask, uint32_t *pu32Count, char **ppchCurrentPos, BUILDCALLBACK fpBuildCallback)
{
  unsigned int uIndex       = 0;
  PBUILDTASK   pLevel23Task = NULL;

  if (!pu16Bitmask || !pu32Count)
  {
    return -1;
  }

  *pu16Bitmask = 0;
  *pu32Count   = 0;

  for (uIndex = uStartBucket; uIndex < uStartBucket + 16; uIndex++)
  {
    (*pu16Bitmask) <<= 1;
    if (pBuckets[uIndex].pPrefixes)
    {
      uint32_t *pu32Pointer = (uint32_t *)(*ppchCurrentPos);

      (*pu16Bitmask) |= 1;
      (*pu32Count)++;

      /* If there is more than one prefix in this bucket, it means pointer will point
         to a next level chunk */
      if (pBuckets[uIndex].u32NumPrefixes > 1)
      {
        *pu32Pointer = POINTERTYPE_NEXTLEVEL;
        if (fpBuildCallback)
        {
          /* All pointers need to follow continuosly after the codewords, so when we
             need to build a next level chunk, put it as a task on the task queue.
             That way we first make all pointers, and then continue with the next level chunks. */
          pLevel23Task = calloc(1, sizeof(*pLevel23Task));
          if (!pLevel23Task)
          {
            printf("Can't allocate level 2/3 build task\n");
            exit(1);
          }
          pLevel23Task->pPrefixes = pBuckets[uIndex].pPrefixes;
          /* Next level task will fill in the pointer with correct offset to the chunk */
          pLevel23Task->pu32Pointer = pu32Pointer;
          pLevel23Task->fpBuild = fpBuildCallback;

          pBuckets[uIndex].pPrefixes = NULL;

          QUEUE_ADD_FRONT(&pBuildTaskHead, &pBuildTaskTail, pLevel23Task);
        }
        else
        {
          printf("Entries continuing below last level?! index = %u\n", uIndex);
        }
      }
      /* If there's only one entry, the pointer will point directly to a next hop */
      else
      {
        *pu32Pointer = POINTERTYPE_NEXTHOP | pBuckets[uIndex].pPrefixes->u32NextHopIndex;
      }

      *ppchCurrentPos += sizeof(uint32_t);
    }
    /* If the bucket is empty, it uses the first pointer to the left of itself,
       so no need for a pointer here */
  }

  return 1;
}

int ProcessBucketGroups(PBUCKET pBuckets, char *pchBucketGroupNumPrefixes, unsigned int uMaxIndex, PCODEWORD pCodewords, char **ppchCurrentLocation, BUILDCALLBACK fpBuildCallback)
{
  unsigned int uNextHop          = 0;
  unsigned int uPointerIndex     = 0;
  unsigned int uLastNextHopIndex = 0;
  unsigned int uIndex            = 0;

  for (uIndex = 0; uIndex < uMaxIndex; uIndex++)
  {
    switch (pchBucketGroupNumPrefixes[uIndex])
    {
      /* No prefixes in this bucket group, so we should encode a next hop index from the last
          next hop found to the left, which covers this bucket group too. */
      case 0:
        pCodewords[uIndex].u64BitmaskOffset = CODEWORD_NEXTHOP | uLastNextHopIndex;
        break;
      /* Single prefix in bucket group can be encoded directly in the codeword, no need for pointer */
      case 1:
        uNextHop = FirstNextHopFromBucketGroup(pBuckets, uIndex * 16);
        pCodewords[uIndex].u64BitmaskOffset = CODEWORD_NEXTHOP | uNextHop;

        uLastNextHopIndex = uNextHop;
        break;
      /* More than one prefix in bucket group, now we need to set the bucket group bitmask
          and pointer offset in the code word, and set pointers to point to the correct next hops
          or next level chunks */
      default:
      {
        uint32_t     u32FoundPrefixes = 0;
        uint16_t     u16Bitmask = 0;

        ProcessMultiPrefixBucket(pBuckets, uIndex * 16, &u16Bitmask, &u32FoundPrefixes, ppchCurrentLocation, fpBuildCallback);
        pCodewords[uIndex].u64BitmaskOffset = (((uint64_t)u16Bitmask) << 32) | (uint64_t)uPointerIndex;
        uPointerIndex += u32FoundPrefixes;
        break;
      }
    }
  }

  return 1;
}

int BuildLevel1(PROUTEENTRY pNextHops, char **ppchCurrentLocation)
{
  return ProcessBucketGroups(pLevel1Buckets, pachBucketGroupNumPrefixes, 4096, pLevel1->codewords, ppchCurrentLocation, ProcessLevel2);
}

#ifdef DEBUG
int DebugBuckets(void)
{
  unsigned int uIndex        = 0;


  for (uIndex = 0; uIndex < 65536; uIndex++)
  {
    if (uIndex % 16 == 0)
    {
      printf (" ");
    }

    if (pLevel1Buckets[uIndex].u32NumPrefixes == 1)
    {
      printf("1");
    }
    else if (pLevel1Buckets[uIndex].u32NumPrefixes > 1)
    {
      printf("+");
    }
    else
    {
      printf("0");
    }
  }
  printf("\n");

  return 1;
}
#endif

int BuildLuleaTrie(PTREENODE pTreeRoot, PROUTEENTRY pNextHops, unsigned int uNumPrefixes)
{
  char        *pchCurrentPos = NULL;

  pLevel1Buckets = calloc(65536, sizeof(BUCKET));
  pachBucketGroupNumPrefixes = calloc(65536 / 16, sizeof(char));

  if (!pLevel1Buckets || !pachBucketGroupNumPrefixes)
  {
    printf("Can't allocate level 1 buckets\n");
    exit(1);
  }

  RecurseRadixTree(pTreeRoot);

  /* 16 MB should be enough for everyone?
     A full BGP dump as of 2020 takes ~8MB */
  pchLuleaTrie = calloc(1, 1024 * 1024 * 16);
  if (!pchLuleaTrie)
  {
    printf("Can't allocate luleå trie memory block!\n");
    exit(1);
  }
  pLevel1 = (PLEVEL1)pchLuleaTrie;

  pchCurrentPos = pchLuleaTrie + sizeof(LEVEL1);

  BuildLevel1(pNextHops, &pchCurrentPos);

  while (pBuildTaskTail)
  {
    PBUILDTASK pTask = NULL;

    QUEUE_REMOVE_TAIL(&pBuildTaskHead, &pBuildTaskTail, pTask);

    pTask->fpBuild(pTask->pu32Pointer, pTask->pPrefixes, &pchCurrentPos);

    free(pTask);
  }

#ifdef DEBUG
  printf("Structure is %ld bytes\n", pchCurrentPos - pchLuleaTrie);
  DebugBuckets();
#endif

  free(pLevel1Buckets);
  free(pachBucketGroupNumPrefixes);

  return 1;
}

PROUTEENTRY LuleaTrieLookup(uint32_t u32IP, PROUTEENTRY pNextHops)
{
  unsigned int uLow            = 0;
  unsigned int uPointer        = 0;
  unsigned int uShiftedBitmask = 0;
  unsigned int uPopcount       = 0;
  uint32_t     u32Offset       = 0;
  PCODEWORD    pCodeWord       = &pLevel1->codewords[u32IP >> 20];
  PLEVEL23     pLevel2         = NULL;
  PLEVEL23     pLevel3         = NULL;

  /* Next hop encoded directly into codeword? */
  if (pCodeWord->u64BitmaskOffset & CODEWORD_NEXTHOP)
  {
    return pNextHops + (pCodeWord->u64BitmaskOffset & 0xFFFFFFFF);
  }

  /* Continue to find correct pointer, either to next hop or next level chunk */
  u32Offset = pCodeWord->u64BitmaskOffset & 0xFFFFFFFF;
  uLow = (u32IP & 0x000F0000) >> 16;

  uShiftedBitmask = pCodeWord->u64BitmaskOffset >> (32 + (16 - (uLow + 1)));
  uPopcount = __builtin_popcount(uShiftedBitmask);
  uPopcount -= (uPopcount > 0);
  uPointer = uPopcount + u32Offset;

  /* Next hop! */
  if (!(pLevel1->au32Pointers[uPointer] & POINTERTYPE_NEXTLEVEL))
  {
    return pNextHops + pLevel1->au32Pointers[uPointer];
  }

  /* Continue with next level */
  pLevel2   = (PLEVEL23) (pchLuleaTrie + (pLevel1->au32Pointers[uPointer] & ~POINTERTYPE_NEXTLEVEL));
  pCodeWord = &pLevel2->codewords[(u32IP >> 12) & 0xF];

  //printf ("Looking at level 2\n");
  if (pCodeWord->u64BitmaskOffset & CODEWORD_NEXTHOP)
  {
    return pNextHops + (pCodeWord->u64BitmaskOffset & 0xFFFFFFFF);
  }

  u32Offset = pCodeWord->u64BitmaskOffset & 0xFFFFFFFF;
  uLow = (u32IP & 0x00000F00) >> 8;

  uShiftedBitmask = pCodeWord->u64BitmaskOffset >> (32 + (16 - (uLow + 1)));
  uPopcount = __builtin_popcount(uShiftedBitmask);
  uPopcount -= (uPopcount > 0);
  uPointer = uPopcount + u32Offset;

  if (!(pLevel2->au32Pointers[uPointer] & POINTERTYPE_NEXTLEVEL))
  {
    return pNextHops + pLevel2->au32Pointers[uPointer];
  }

  pLevel3 = (PLEVEL23) (pchLuleaTrie + (pLevel2->au32Pointers[uPointer] & ~POINTERTYPE_NEXTLEVEL));
  pCodeWord = &pLevel3->codewords[(u32IP >> 4) & 0xF];

  //printf ("Looking at level 3\n");

  if (pCodeWord->u64BitmaskOffset & CODEWORD_NEXTHOP)
  {
    return pNextHops + (pCodeWord->u64BitmaskOffset & 0xFFFFFFFF);
  }

  u32Offset = pCodeWord->u64BitmaskOffset & 0xFFFFFFFF;
  uLow = (u32IP & 0x0000000F);

  uShiftedBitmask = pCodeWord->u64BitmaskOffset >> (32 + (16 - (uLow + 1)));
  uPopcount = __builtin_popcount(uShiftedBitmask);
  uPopcount -= (uPopcount > 0);
  uPointer = uPopcount + u32Offset;

  if (!(pLevel3->au32Pointers[uPointer] & POINTERTYPE_NEXTLEVEL))
  {
    return pNextHops + pLevel3->au32Pointers[uPointer];
  }

  return NULL;
}
