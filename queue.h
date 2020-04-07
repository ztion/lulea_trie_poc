#ifndef __QUEUE_H__
#define __QUEUE_H__

#define QUEUE_ADD_FRONT(ppHead, ppTail, pEntry) \
  do {                                    \
    if (*ppHead)                          \
    {                                     \
      *ppHead->pPrev = pEntry;            \
      pEntry->pNext = *ppHead;            \
      *ppHead = pEntry;                   \
    }                                     \
    else                                  \
    {                                     \
      *ppHead = pEntry;                   \
      *ppTail = pEntry;                   \
    }                                     \
  } while(0)

#define QUEUE_REMOVE_TAIL(ppHead, ppTail, pEntry)      \
  do {                                         \
    if (*ppTail && *ppHead)                    \
    {                                          \
      if (*ppTail == *ppHead)                  \
      {                                        \
        pEntry = *ppTail;                      \
        *ppTail = NULL;                        \
        *ppHead = NULL;                        \
      }                                        \
      else if (*ppTail->pPrev)                 \
      {                                        \
        *ppTail->pPrev->pNext = NULL;          \
        pEntry = *ppTail;                      \
        *ppTail = *ppTail->pPrev;              \
      }                                        \
    }                                          \
  } while(0)

#endif /* __QUEUE_H__ */
