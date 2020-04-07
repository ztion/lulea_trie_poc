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

#ifndef __READ_BGP_H__
#define __READ_BGP_H__

struct tagROUTEENTRY;

typedef struct tagPREFIXES {
  struct tagROUTEENTRY *pPrefixes[33];
  unsigned int uNumPrefixes[33];
  unsigned int uTotalPrefixes;
} PREFIXES, *PPREFIXES;

PPREFIXES ReadFromBgpDump(char *filename);

#endif /* __READ_BGP_H__ */