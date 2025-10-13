/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"

static bool file_backed_swap_in(struct page *page, void *kva);
static bool file_backed_swap_out(struct page *page);
static void file_backed_destroy(struct page *page);

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
  struct thread *cur = thread_current();

  if (page->frame == NULL) return;

  if (pml4_is_dirty(cur->pml4, page->va)) {
    // 페이지가 수정, 기록되었는지(dirty) 확인
    // filesys_lock_acquire();
    // 락 해야하나?
    file_write_at(file_page->file, page->frame->kva, file_page->read_bytes,
                  file_page->offset);
    // 변경된 내용을 파일의 올바른 위치(offset)에 다시 쓰는 로직
    pml4_set_dirty(cur->pml4, page->va, 0);
    // 내용을 파일에 작성한 후 dirty bit를 0으로 변경
    // filesys_lock_release();  // 락 해야하나?
  }

  pml4_clear_page(cur->pml4, page->va);
  // 페이지 테이블에서 해당 가상 주소 매핑을 제거
}

/* Do the mmap */
void *do_mmap(void *addr, size_t length, int writable, struct file *file,
              off_t offset) {}

/* Do the munmap */
void do_munmap(void *addr) {}
