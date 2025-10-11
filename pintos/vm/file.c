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
void vm_file_init(void) {
  /* 아직 준비할 건 없음
   * 필요 시 락/리스트 등을 여기서 초기화 */
}

/* Initialize the file backed page */
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva) {
  /* 1) 페이지 핸들러를 파일 전용으로 바꿔준다. */
  page->operations = &file_ops;

  /* 2) do_mmap()이 vm_alloc_page_with_initializer()의 aux로 넘겨준
        struct file_page를 꺼내 page->file에 복사한다. */
  struct file_page *source = (struct file_page *)page->uninit.aux;

  if (source == NULL) {
    /* aux 없이 호출되면 초기화할 정보가 없으므로 실패 처리 */
    return false;
  }

  page->file = *source; /* file, offset, read_bytes, zero_bytes 모두 복사 */
  page->uninit.aux = NULL;
  free(source); /* aux는 더 이상 필요 없음 */

  /* 주의: dst->file 은 do_mmap() 쪽에서 이미 file_reopen()으로
     독립 핸들을 만들어 넘겨주는 것이 가장 안전(페이지별 close 가능) */

  return true;
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
              off_t offset) {}

/* Do the munmap */
void do_munmap(void *addr) {}
