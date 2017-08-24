/* libunwind - a platform-independent unwind library
   Copyright (C) 2003-2005 Hewlett-Packard Co
	Contributed by David Mosberger-Tang <davidm@hpl.hp.com>

This file is part of libunwind.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.  */

#include <limits.h>
#include <stdio.h>

#include "libunwind_i.h"
#include "os-linux.h"

typedef struct {
  unsigned long low;
  unsigned long high;
  unsigned long offset;
  char *path;
} CacheItem;

typedef struct {
  CacheItem *array;
  size_t used;
  size_t size;
} Array;

inline void initArray(Array *a, size_t initialSize) {
  a->array = (CacheItem *)malloc(initialSize * sizeof(CacheItem));
  Debug(8, "mmap ptr=%p size=%d\n", a->array, initialSize * sizeof(CacheItem));
  a->used = 0;
  a->size = initialSize;
}

inline void insertArray(Array *a, CacheItem element) {
  // a->used is the number of used entries, because a->array[a->used++] updates a->used only *after* the array has been accessed.
  // Therefore a->used can go up to a->size 
  if (a->used == a->size) {
    a->size *= 2;
    a->array = (CacheItem *)realloc(a->array, a->size * sizeof(CacheItem));
  }
  a->array[a->used++] = element;
}

inline void freeArray(Array *a) {
  int i=0;
  for (; i < a->used; ++i) {
    if (a->array[i].path) {
      free(a->array[i].path);
      Debug(8, "munmap ptr=%p size=0\n", a->array[i].path);
    }
  }
  free(a->array);
  Debug(8, "munmap ptr=%p size=0\n", a->array);
  a->array = NULL;
  a->used = a->size = 0;
}

Array * _g_elf_image_cache = NULL;

void clear_elf_image_cache()
{
    if (!_g_elf_image_cache)
        return;
    freeArray(_g_elf_image_cache);
    free(_g_elf_image_cache);
    Debug(8, "munmap ptr=%p size=0\n", _g_elf_image_cache);
    _g_elf_image_cache = NULL;
}

inline void _refresh_elf_image_cache(pid_t pid)
{
  if (_g_elf_image_cache)
    return;
  // build the cache
  struct map_iterator mi;
  unsigned long low, high, offset;
  CacheItem item;
  if (maps_init(&mi, pid) < 0)
    return;
  _g_elf_image_cache = (Array *)malloc(sizeof(Array));
  Debug(8, "mmap ptr=%p size=%d\n", _g_elf_image_cache, sizeof(Array));
  initArray(_g_elf_image_cache, 500);
  // iter maps
  while (maps_next(&mi, &low, &high, &offset))
  {
    item.low = low;
    item.high = high;
    item.offset = offset;
    item.path = malloc(strlen(mi.path) + 1);
    Debug(8, "mmap ptr=%p size=%d\n", item.path, strlen(mi.path) + 1);
    strcpy(item.path, mi.path);
    insertArray(_g_elf_image_cache, item);
  }
  maps_close(&mi);
}

inline CacheItem* get_elf_image_by_cache(pid_t pid, unw_word_t ip,
                           unsigned long *segbase, unsigned long *mapoff)
{
    _refresh_elf_image_cache(pid);
    if(!_g_elf_image_cache)
        return NULL;
    CacheItem *s, *e;
    s = _g_elf_image_cache->array;
    e = s + _g_elf_image_cache->used;
    for (; s < e; ++s)
    {
        *segbase = s->low;
        *mapoff = s->offset;
        if (ip >= s->low && ip < s->high) {
            return s;
        }
    }
    return NULL;
}

PROTECTED void tdep_clear_elf_image_cache()
{
    clear_elf_image_cache();
}

int local_unw_str_endswith(const char *str, const char *suffix) {
    if (!str || !suffix)
        return 0;
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix > lenstr)
        return 0;
    if (strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0)
        return lenstr - lensuffix;
    return 0;
}

PROTECTED int tdep_get_elf_image(struct elf_image *ei, pid_t pid, unw_word_t ip,
                                 unsigned long *segbase, unsigned long *mapoff,
                                 char *path, size_t pathlen)
{
    int rc;
    char *sFoundPath = NULL;
    CacheItem *pFoundItem = NULL;

    pFoundItem = get_elf_image_by_cache(pid, ip, segbase, mapoff);
    if (!pFoundItem) {
        return -1;
    }
    if (path) {
        strncpy(path, pFoundItem->path, pathlen);
    }
    int suffix_head_offset = local_unw_str_endswith(pFoundItem->path, " (deleted)");
    if (suffix_head_offset > 0) {
        pFoundItem->path[suffix_head_offset] = '\0';
    }
    Debug(4, "get_elf_image '%s' low=%lx high=%lx\n", path, pFoundItem->low, pFoundItem->high);
    if (strcmp(pFoundItem->path, "[vdso]") == 0) {
        rc = elf_map_image_vdso(pid, ei, pFoundItem->low, pFoundItem->high - pFoundItem->low);
    } else {
        rc = elf_map_image(ei, pFoundItem->path);
    }
    return rc;
}
