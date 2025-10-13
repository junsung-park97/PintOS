/* file.c: Implementation of memory backed file object (mmaped object). */

#include "filesys/file.h"

#include "threads/vaddr.h"
#include "vm/vm.h"

static bool file_backed_swap_in(struct page *page, void *kva);
static bool file_backed_swap_out(struct page *page);
static void file_backed_destroy(struct page *page);
static bool lazy_load_mmap(struct page *page, void *aux_);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
    .swap_in = file_backed_swap_in,
    .swap_out = file_backed_swap_out,
    .destroy = file_backed_destroy,
    .type = VM_FILE,
};

/* The initializer of file vm */
void vm_file_init(void) {}

/* Initialize the file backed page */
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva) {
  /* Set up the handler */
  page->operations = &file_ops;

  struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool file_backed_swap_in(struct page *page, void *kva) {
  struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool file_backed_swap_out(struct page *page) {
  struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void file_backed_destroy(struct page *page) {
  struct file_page *file_page UNUSED = &page->file;
}

/* Do the mmap */
void *do_mmap(void *addr, size_t length, int writable, struct file *file,
              off_t offset) {
  bool sucess = false;
  void *upage = addr;

  // 유휴성 검증
  if (pg_ofs(upage) != 0) return NULL;
  if (pg_ofs(offset) != 0) return NULL;
  if (length <= 0) return NULL;
  if (file == NULL) return NULL;

  // 파일 객체 복사
  struct file *mmap_file = file_reopen(file);
  if (mmap_file == NULL) return NULL;

  // 파일 객체의 byte 길이
  off_t file_len = file_length(mmap_file);
  if (file_len == 0) {
    file_close(mmap_file);
    return NULL;
  }

  // 할당되야하는 페이지 수
  size_t page_count = (length + (PGSIZE - 1)) / PGSIZE;

  // 할당해야 하는 페이지 수 만큼 반복
  for (int i = 0; i < page_count; i++) {
    // aux 생성
    struct file_page *aux = malloc(sizeof *aux);
    if (!aux) {
      do_munmap(upage);
      file_close(mmap_file);
      return NULL;
    };

    // 페이지별 read_byte 계산
    size_t file_read_byte = length < file_len ? length : file_len;
    size_t file_zero_byte = pg_round_up(file_read_byte) - file_read_byte;

    // aux 초기화
    aux->file = mmap_file;
    aux->ofs = offset + (PGSIZE * i);
    aux->read_bytes = file_read_byte;
    aux->zero_bytes = file_zero_byte;

    // 페이지 할당
    if (!vm_alloc_page_with_initializer(VM_FILE, upage, writable,
                                        lazy_load_mmap, aux)) {
      free(aux);
      do_munmap(upage);
      file_close(mmap_file);
      return NULL;
    }

    length -= file_read_byte;
    upage += PGSIZE;
  }

  return upage;
}

static bool lazy_load_mmap(struct page *page, void *aux_) {}

/* Do the munmap */
void do_munmap(void *addr) {}
