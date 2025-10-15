#ifndef VM_FILE_H
#define VM_FILE_H
#include <stdbool.h>

#include "filesys/file.h"
#include "lib/kernel/list.h"
#include "vm/types.h"

struct page;

struct file_page {
  struct file *file;
  off_t offset;
  size_t read_bytes;
  size_t zero_bytes;
  bool owns_file;
};

void vm_file_init(void);
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva);
void *do_mmap(void *addr, size_t length, int writable, struct file *file,
              off_t offset);
void do_munmap(void *va);
#endif
