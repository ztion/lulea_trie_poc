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

#ifndef __ROUTING_TABLE_SPLIT_H__
#define __ROUTING_TABLE_SPLIT_H__

#include <stdint.h>

typedef struct tagROUTEENTRY
{
    uint32_t u32Start;   /* Start IP of route: host order */
    uint32_t u32Size;

    uint32_t u32NextHopIndex;

    struct tagROUTEENTRY *pNext, *pPrev;
} ROUTEENTRY, *PROUTEENTRY;

#define NO_NEXT_HOP (UINT32_MAX)

typedef struct tagTREENODE
{
    uint32_t            u32Prefix;
    uint32_t            u32NextHopIndex;

    struct tagTREENODE *pLeft;
    struct tagTREENODE *pRight;

    PROUTEENTRY         pRoute;

} TREENODE, *PTREENODE;

void PrintIP(uint32_t u32IP);

#endif /* __ROUTING_TABLE_SPLIT_H__ */