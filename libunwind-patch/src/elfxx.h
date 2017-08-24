/* libunwind - a platform-independent unwind library
   Copyright (C) 2003, 2005 Hewlett-Packard Co
   Copyright (C) 2007 David Mosberger-Tang
	Contributed by David Mosberger-Tang <dmosberger@gmail.com>

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

#include <stdlib.h>
#include <errno.h>
#include <elf.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/uio.h>

#if ELF_CLASS == ELFCLASS32
# define ELF_W(x)	ELF32_##x
# define Elf_W(x)	Elf32_##x
# define elf_w(x)	_Uelf32_##x
#else
# define ELF_W(x)	ELF64_##x
# define Elf_W(x)	Elf64_##x
# define elf_w(x)	_Uelf64_##x
#endif

#include "libunwind_i.h"

extern int elf_w (get_proc_name) (unw_addr_space_t as,
				  pid_t pid, unw_word_t ip,
				  char *buf, size_t len,
				  unw_word_t *offp);

extern int elf_w (get_proc_name_in_image) (unw_addr_space_t as,
					   struct elf_image *ei,
					   unsigned long segbase,
					   unsigned long mapoff,
					   unw_word_t ip,
					   char *buf, size_t buf_len, unw_word_t *offp);

extern int elf_w (get_proc_name) (unw_addr_space_t as,
				  pid_t pid, unw_word_t ip,
				  char *buf, size_t len,
				  unw_word_t *offp);

static inline int
elf_w (valid_object) (struct elf_image *ei)
{
  if (ei->size <= EI_VERSION)
    return 0;

  return (memcmp (ei->image, ELFMAG, SELFMAG) == 0
	  && ((uint8_t *) ei->image)[EI_CLASS] == ELF_CLASS
	  && ((uint8_t *) ei->image)[EI_VERSION] != EV_NONE
	  && ((uint8_t *) ei->image)[EI_VERSION] <= EV_CURRENT);
}

#define USE_VDSO_BUFF

#ifdef USE_VDSO_BUFF
static char* _g_vdso_buff = NULL;
static pid_t _g_vdso_pid = 0;
static unsigned long _g_vdso_len = 0;
#endif

static inline char*
elf_get_vdso(pid_t pid, unsigned long low, unsigned long* length)
{
    // read tracee process vdso memory
    char *buff;
    #ifdef USE_VDSO_BUFF
    if (_g_vdso_pid == pid && _g_vdso_buff) {
        if (*length > _g_vdso_len)
            *length = _g_vdso_len;
        return _g_vdso_buff;
    }
    #endif

    buff = malloc(*length);
    Debug(8, "mmap ptr=%p size=%d\n", buff, *length);
    struct iovec local_iov = {(void*)buff, *length};
    struct iovec remote_iov = {(void*)low, *length};
    *length = process_vm_readv(pid, &local_iov, 1, &remote_iov, 1, 0);
    if (errno || *length <= 0) {
        free(buff);
        Debug(8, "munmap ptr=%p size=0\n", buff);
        return NULL;
    }
    #ifdef USE_VDSO_BUFF
    if (_g_vdso_buff) {
        free(_g_vdso_buff);
        Debug(8, "munmap ptr=%p size=0\n", _g_vdso_buff);
    }
    _g_vdso_pid = pid;
    _g_vdso_buff = buff;
    _g_vdso_len = *length;
    #endif
    return buff;
}

static inline int
elf_map_image_vdso (pid_t pid, struct elf_image *ei, unsigned long low, unsigned long length)
{
    // read tracee process vdso memory
    char *buff = elf_get_vdso(pid, low, &length);
    if (!buff) {
        Debug (4, "elf_map_image_vdso memcpy failed\n");
        return -1;
    }
    ei->size = length;
    ei->image = mmap(NULL, ei->size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    Debug(8, "mmap ptr=%p size=%d\n", ei->image, ei->size);
    if  (!ei->image == MAP_FAILED) {
        #ifndef USE_VDSO_BUFF
        free(buff);
        Debug(8, "munmap ptr=%p size=0\n", buff);
        #endif
        Debug (4, "elf_map_image_vdso mmap failed\n");
        return -1;
    }
    memcpy(ei->image, buff, length);
    #ifndef USE_VDSO_BUFF
    free(buff);
    Debug(8, "munmap ptr=%p size=0\n", buff);
    #endif

    if (!elf_w (valid_object) (ei)) {
        Debug (4, "elf_map_image_vdso invalid object\n");
        munmap(ei->image, ei->size);
        Debug(8, "munmap ptr=%p size=%d\n", ei->image, ei->size);
        return -1;
    }
    Debug (4, "elf_map_image_vdso success\n");

    return 0;
}

static inline int
elf_map_image (struct elf_image *ei, const char *path)
{
  struct stat stat;
  int fd;

  fd = open (path, O_RDONLY);
  if (fd < 0)
  {
    Debug (4, "elf_map_image %s fd<0\n", path);
    return -1;
  }

  if (fstat (fd, &stat) < 0)
  {
    Debug (4, "elf_map_image %s cannot stat\n", path);
    close (fd);
    return -1;
  }

  ei->size = stat.st_size;
  ei->image = mmap (NULL, ei->size, PROT_READ, MAP_PRIVATE, fd, 0);
  Debug(8, "mmap ptr=%p size=%d\n", ei->image, ei->size);
  close (fd);
  if (ei->image == MAP_FAILED) {
    Debug (4, "elf_map_image %s map failed\n", path);
    return -1;
  }

  if (!elf_w (valid_object) (ei))
  {
    Debug (4, "elf_map_image %s invalid object\n", path);
    munmap(ei->image, ei->size);
    Debug(8, "munmap ptr=%p size=%d\n", ei->image, ei->size);
    return -1;
  }

  return 0;
}
