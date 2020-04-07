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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#include "routing_table_split.h"
#include "read_bgp.h"
#include "linked_list.h"
#include "lulea_trie.h"

TREENODE root;

static PROUTEENTRY  pNextHops;   /* Next hop array */
static unsigned int uNextHopIndex = 0;

void PrintIP(uint32_t u32IP)
{
  printf("%u.%u.%u.%u\n", (u32IP & 0xFF000000) >> 24, (u32IP & 0xFF0000) >> 16,
                          (u32IP & 0xFF00) >> 8, u32IP & 0xFF);
}

/*
 * Params:
 * PROUTEENTRY pRoute      - The route entry used for traversing the tree
 * PROUTEENTRY pRouteEntry - Set information from this route on the TREENODE
 *                         - when found correct location.
 *                         - You can free this after the function returns.
 */
int InsertIntoPrefixTreeRecurse(PTREENODE pTreeStart, unsigned int uLevel, unsigned int uMask, PROUTEENTRY pRoute, PROUTEENTRY pRouteEntry)
{
  PTREENODE    pIterate     = pTreeStart;
  uint32_t     u32RouteMask = ~(pRoute->u32Size - 1);
  uint32_t     u32CIDR      = __builtin_popcount(u32RouteMask);

  //printf("cidr: %u\n", u32CIDR);

  while (uLevel != u32CIDR)
  {
    if (pRoute->u32Start & uMask)
    {
      //printf("Going right!\n");
      if (!pIterate->pRight)
      {
        //printf("Needed alloc\n");
        if ((uLevel % 2) == 0)
        {
          /* By allocating parent and child nodes in the same memory block
             the 3 nodes will (almost) fit within a 64 byte cache line,
             making it faster to traverse down the tree. */
          pIterate->pRight = calloc(1, sizeof(TREENODE) * 3);
        }
        else
        {
          pIterate->pRight = pIterate + 1;
        }
      }

      pIterate = pIterate->pRight;
    }
    else
    {
      //printf("Going left!\n");
      if (!pIterate->pLeft)
      {
        //printf("Needed alloc\n");
        if ((uLevel % 2) == 0)
        {
          pIterate->pLeft = calloc(1, sizeof(TREENODE) * 3);
        }
        else
        {
          pIterate->pLeft = pIterate + 2;
        }
      }

      pIterate = pIterate->pLeft;
    }
    uMask >>= 1;
    uLevel++;
  }

  if (pIterate->pLeft || pIterate->pRight)
  {
    ROUTEENTRY routeFirst  = { 0 };
    ROUTEENTRY routeSecond = { 0 };

    routeFirst.u32Start  = pRoute->u32Start;
    routeFirst.u32Size   = pRoute->u32Size / 2;

    routeSecond.u32Size  = pRoute->u32Size / 2;
    routeSecond.u32Start = pRoute->u32Start + routeSecond.u32Size;

    //printf("Needed to split. First size: %u. Second size: %u\n", routeFirst.u32Size, routeSecond.u32Size);
    //PrintIP(routeFirst.u32Start);
    //PrintIP(routeSecond.u32Start);

    InsertIntoPrefixTreeRecurse(pIterate, uLevel, uMask, &routeFirst, pRouteEntry);
    InsertIntoPrefixTreeRecurse(pIterate, uLevel, uMask, &routeSecond, pRouteEntry);
  }
  else if (pIterate->pRoute)
  {
    //printf("Level %u already occupied, stopping!\n", uLevel);
  }
  else
  {
    //printf("Inserted at level %u\n", uLevel);
    pIterate->pRoute = calloc(1, sizeof(*pIterate->pRoute));
    if (!pIterate->pRoute)
    {
      printf("Couldn't allocate route\n");
      exit(1);
    }
    pIterate->pRoute->u32Start = pRoute->u32Start;
    pIterate->pRoute->u32Size = pRoute->u32Size;
    pIterate->pRoute->u32NextHopIndex = pRouteEntry->u32NextHopIndex;
  }
  
  return 0;
}

void FreePrefixTreeRecurse(PTREENODE pTreeNode, unsigned int uLevel)
{
  if (pTreeNode->pLeft)
  {
    FreePrefixTreeRecurse(pTreeNode->pLeft, uLevel + 1);
  }
  if (pTreeNode->pRight)
  {
    FreePrefixTreeRecurse(pTreeNode->pRight, uLevel + 1);
  }

  free(pTreeNode->pRoute);

  if (uLevel % 2 == 1)
  {
    free(pTreeNode);
  }
}

void FreePrefixTree(void)
{
  FreePrefixTreeRecurse(root.pLeft, 1);
  FreePrefixTreeRecurse(root.pRight, 1);

  root.pLeft  = NULL;
  root.pRight = NULL;
}

int InsertIntoPrefixTree(PROUTEENTRY pRoute)
{
  return InsertIntoPrefixTreeRecurse(&root, 0, 0x80000000, pRoute, pRoute);
}

PROUTEENTRY LookupInTree(uint32_t u32IP)
{
  PTREENODE    pIterate     = &root;
  unsigned int uMask        = 0x80000000;


  while (pIterate)
  {
    if (pIterate->pRoute)
    {
      //return pIterate->pRoute;
      return &pNextHops[pIterate->pRoute->u32NextHopIndex];
    }

    if (u32IP & uMask)
    {
      pIterate = pIterate->pRight;
    }
    else
    {
      pIterate = pIterate->pLeft;
    }

    uMask >>= 1;
  }

  return NULL;
}

#ifdef DEBUG
void PrintLinkedList(PROUTEENTRY pHead)
{
  while (pHead)
  {
    PrintIP(pHead->u32Start);
    printf("size: %u\n\n", pHead->u32Size);

    pHead = pHead->pNext;
  }
}
#endif

/* Frees route entries from list after inserting into tree */
void LinkedListToTree(PROUTEENTRY pHead)
{
  PROUTEENTRY pTmp = NULL;

  while (pHead)
  {
    pNextHops[uNextHopIndex] = *pHead;
    pNextHops[uNextHopIndex].pNext = NULL;
    pNextHops[uNextHopIndex].pPrev = NULL;

    pHead->u32NextHopIndex = uNextHopIndex;

    InsertIntoPrefixTree(pHead);
    uNextHopIndex++;

    pTmp = pHead->pNext;
    free(pHead);
    pHead = pTmp;
  }
}

void QueryTree(PROUTEENTRY pNextHops)
{
  char achBuffer[256];
  struct in_addr ipAddr;
  uint32_t u32IP;
  PROUTEENTRY pRoute = NULL;


  printf("Enter IPv4 to query for route:\n");

  while (1)
  {
    fgets(achBuffer, sizeof(achBuffer), stdin);
    achBuffer[255] = '\0';

    if (!strcmp(achBuffer, "quit"))
    {
      exit(EXIT_SUCCESS);
    }

    inet_aton(achBuffer, &ipAddr);
    u32IP = ntohl(ipAddr.s_addr);

#ifdef DEBUG
    pRoute = LookupInTree(u32IP);
    if (pRoute)
    {
      printf ("Tree: Found route of size %u!\n", pRoute->u32Size);
      PrintIP(pRoute->u32Start);
    }
    else
    {
      printf("Tree: Did not find route!\n");
    }
#endif

    pRoute = LuleaTrieLookup(u32IP, pNextHops);
    if (pRoute)
    {
      printf ("Luleå: Found route of size %u!\n", pRoute->u32Size);
      PrintIP(pRoute->u32Start);
    }
    else
    {
      printf("Luleå: Did not find route!\n");
    }

  }
}

#ifdef DEBUG
int VerifyLulea(PROUTEENTRY pNextHops)
{
  uint32_t u32IP = 0;

  printf ("Starting luleå trie verification now..\n");

  for (u32IP = 0; u32IP < UINT32_MAX; u32IP++)
  {
    PROUTEENTRY pRouteTree  = NULL;
    PROUTEENTRY pRouteLulea = NULL;

    pRouteTree = LookupInTree(u32IP);
    pRouteLulea = LuleaTrieLookup(u32IP, pNextHops);

    if (pRouteTree != pRouteLulea)
    {
      printf("Mismatch!\n");
      PrintIP(u32IP);
    }
  }

  printf("done..\n");
}
#endif

void timediff(struct timespec *sooner, struct timespec *later, struct timespec *result)
{       
  int carry = 0;
  
  if (later->tv_nsec < sooner->tv_nsec)
  {       
    carry = 1;
    result->tv_nsec = (later->tv_nsec + 1000000000) - sooner->tv_nsec;
  }       
  else
  {       
    result->tv_nsec = later->tv_nsec - sooner->tv_nsec;
  }       
  
  result->tv_sec = later->tv_sec - carry - sooner->tv_sec;
}       

#define BENCHMARK_IPS (100000)
void Benchmark(void)
{
  struct       timespec  sooner;
  struct       timespec  later;
  struct       timespec  diff;
  uint32_t    *pu32IPs = NULL;
  unsigned int uIndex  = 0;


  pu32IPs = calloc(BENCHMARK_IPS, sizeof(*pu32IPs));
  if (!pu32IPs)
  {
    printf("Can't allocate benchmark IP list\n");
    exit(1);
  }

  /* Always use the same seed so that benchmark is reproducible. */
  srand(100);
  for (uIndex = 0; uIndex < BENCHMARK_IPS; uIndex++)
  {
    pu32IPs[uIndex] = rand();
  }

  clock_gettime(CLOCK_MONOTONIC, &sooner);
  for (uIndex = 0; uIndex < BENCHMARK_IPS; uIndex++)
  {
    LookupInTree(pu32IPs[uIndex]);
  }
  clock_gettime(CLOCK_MONOTONIC, &later);
  timediff(&sooner, &later, &diff);
  printf("Benchmark: %d Lookups in radix trie took %ld sec %ld nanosec\n", BENCHMARK_IPS, diff.tv_sec, diff.tv_nsec);

  clock_gettime(CLOCK_MONOTONIC, &sooner);
  for (uIndex = 0; uIndex < BENCHMARK_IPS; uIndex++)
  {
    LuleaTrieLookup(pu32IPs[uIndex], pNextHops);
  }
  clock_gettime(CLOCK_MONOTONIC, &later);
  timediff(&sooner, &later, &diff);
  printf("Benchmark: %d Lookups in luleå trie took %ld sec %ld nanosec\n", BENCHMARK_IPS, diff.tv_sec, diff.tv_nsec);

  free(pu32IPs);
}

int main(int argc, char **argv)
{
  PPREFIXES pPrefixes = NULL;
  uint32_t  u32Index  = 0;
  struct    timespec  sooner;
  struct    timespec  later;
  struct    timespec  diff;

  if (argc < 2)
  {
    printf("Usage: %s <bgp dump file>\n", argv[0]);
    exit(1);
  }

  printf("Reading BGP from file\n");
  pPrefixes = ReadFromBgpDump(argv[1]);
  printf("done..\n");

  pNextHops = malloc(sizeof(*pNextHops) * pPrefixes->uTotalPrefixes);
  if (!pNextHops)
  {
    printf("Can't allocate nexthop array\n");
    exit(1);
  }

  clock_gettime(CLOCK_MONOTONIC, &sooner);
  /* Insert routes from linked lists into radix tree.
     Start with the narrowest routes and continue with wider routes, so that
     wider routes will be split and the parts covering narrower routes removed. */
  for (u32Index = 32; u32Index != UINT32_MAX; u32Index--)
  {
    printf("%u prefixes at level %u\n", pPrefixes->uNumPrefixes[u32Index], u32Index);
    LinkedListToTree(pPrefixes->pPrefixes[u32Index]);    
  }
  clock_gettime(CLOCK_MONOTONIC, &later);
  timediff(&sooner, &later, &diff);
  printf("Building radix took %ld sec %ld nanosec\n", diff.tv_sec, diff.tv_nsec);

  printf("Building luleå trie now..\n");
  clock_gettime(CLOCK_MONOTONIC, &sooner);
  BuildLuleaTrie(&root, pNextHops, pPrefixes->uTotalPrefixes);
  clock_gettime(CLOCK_MONOTONIC, &later);
  printf("done.\n");
  timediff(&sooner, &later, &diff);
  printf("Building luleå trie took %ld sec %ld nanosec\n", diff.tv_sec, diff.tv_nsec);

  Benchmark();
#ifdef DEBUG
  VerifyLulea(pNextHops);
#endif

#ifndef DEBUG
  FreePrefixTree();
#endif

  QueryTree(pNextHops);
}