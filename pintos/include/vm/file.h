#ifndef VM_FILE_H
#define VM_FILE_H
#include <stdbool.h>

#include "filesys/file.h"
#include "lib/kernel/list.h"
#include "vm/types.h"

struct page;

struct mmap_file {
  struct file *file; /* file_reopen() 한 핸들 (매핑 단위로 1개) */
  void *base;        /* 매핑 시작 유저 주소 (page-aligned) */
  size_t length;     /* 요청 길이 (바이트, 마지막 페이지는 일부만 사용 가능) */
  struct list_elem elem;
};

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
