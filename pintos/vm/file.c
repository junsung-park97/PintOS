/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/file.h"

#include <string.h>

#include "threads/malloc.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "vm/vm.h"

static bool file_backed_swap_in(struct page *page, void *kva);
static bool file_backed_swap_out(struct page *page);
static void file_backed_destroy(struct page *page);
static bool lazy_load_mmap(struct page *page, void *aux_);

extern struct lock filesys_lock;

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
  page->operations = &file_ops;
  // exec 경로 안전을 위해 기본값 초기화만 (mmap은 lazy_load_mmap에서 채움)
  page->file.file = NULL;
  page->file.offset = 0;
  page->file.read_bytes = 0;
  page->file.zero_bytes = 0;
  page->file.owns_file = false;
  return true;
}

/* Swap in the page by read contents from the file. */
static bool file_backed_swap_in(struct page *page, void *kva) {
  struct file_page *file_page UNUSED = &page->file;

  if (file_page->read_bytes > 0) {
    if (file_page->file == NULL) return false;
    lock_acquire(&filesys_lock);
    off_t n = file_read_at(file_page->file, kva, file_page->read_bytes,
                           file_page->offset);
    lock_release(&filesys_lock);
    if (n != (off_t)file_page->read_bytes) {
      return false;
    }
  }

  if (file_page->zero_bytes > 0) {
    memset((uint8_t *)kva + file_page->read_bytes, 0, file_page->zero_bytes);
  }

  return true;
}

/* Swap out the page by writeback contents to the file. */
static bool file_backed_swap_out(struct page *page) {
  struct file_page *file_page UNUSED = &page->file;
  struct frame *frame = page->frame;
  struct thread *owner = page->owner;
  if (!frame || !owner) return true;

  bool dirty = pml4_is_dirty(owner->pml4, page->va);
  if (dirty && file_page->file) {
    lock_acquire(&filesys_lock);
    off_t written = file_write_at(file_page->file, frame->kva,
                                  file_page->read_bytes, file_page->offset);
    lock_release(&filesys_lock);
    if (written != (off_t)file_page->read_bytes) return false;
    pml4_set_dirty(owner->pml4, page->va, false);
  }

  /* 매핑 제거는 vm_evict_frame()이 일괄 처리 */
  return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void file_backed_destroy(struct page *page) {
  struct file_page *file_page = &page->file;
  struct frame *frame = page->frame;
  struct thread *owner = page->owner;
  uint64_t *pml4 = owner ? owner->pml4 : NULL;

  if (frame != NULL) {
    if (file_page->file != NULL && pml4 != NULL &&
        pml4_is_dirty(pml4, page->va) && file_page->read_bytes > 0) {
      lock_acquire(&filesys_lock);
      off_t written = file_write_at(file_page->file, frame->kva,
                                    file_page->read_bytes, file_page->offset);
      lock_release(&filesys_lock);
      if (written == (off_t)file_page->read_bytes) {
        pml4_set_dirty(pml4, page->va, false);
      }
    }

    if (pml4 != NULL) {
      pml4_clear_page(pml4, page->va);
    }
    frame->page = NULL;
    page->frame = NULL;
    vm_free_frame(frame);
  } else if (pml4 != NULL) {
    pml4_clear_page(pml4, page->va);
  }

  if (file_page->owns_file && file_page->file != NULL) {
    lock_acquire(&filesys_lock);
    file_close(file_page->file);
    lock_release(&filesys_lock);
  }
  file_page->file = NULL;
  file_page->owns_file = false;
}

/* Do the mmap */
void *do_mmap(void *addr, size_t length, int writable, struct file *file,
              off_t offset) {
  struct thread *cur = thread_current();
  void *base = addr;
  void *upage = addr;

  // 유휴성 검증
  if (upage == NULL) return NULL;
  if (!is_user_vaddr(upage)) return NULL;
  if (pg_ofs(upage) != 0) return NULL;

  if (pg_ofs(upage) != 0) return NULL;
  if (pg_ofs(offset) != 0) return NULL;
  if (length <= 0) return NULL;
  if (file == NULL) return NULL;

  // 파일 객체의 byte 길이
  lock_acquire(&filesys_lock);
  off_t file_len = file_length(file);
  lock_release(&filesys_lock);
  if (file_len == 0) {
    return NULL;
  }

  // 할당되야하는 페이지 수
  size_t page_count = (length + (PGSIZE - 1)) / PGSIZE;

  // [추가] 겹침 사전 검사: 대상 범위에 뭐라도 있으면 실패
  for (size_t i = 0; i < page_count; i++) {
    if (!is_user_vaddr(upage)) return NULL;
    if (spt_find_page(&cur->spt, upage) != NULL) return NULL;
    upage += PGSIZE;
  }
  upage = addr;

  // 페이지별 할당
  size_t remain = length;
  off_t ofs = offset;

  // 할당해야 하는 페이지 수 만큼 반복
  for (int i = 0; i < page_count; i++) {
    // aux 생성
    struct file_page *aux = malloc(sizeof *aux);
    if (!aux) {
      do_munmap(upage);
      return NULL;
    };

    // [수정] 페이지 단위로 읽을 양 계산 (≤ PGSIZE)
    size_t step = remain < PGSIZE ? remain : PGSIZE;
    size_t file_left = ofs < file_len ? (size_t)(file_len - ofs) : 0;
    size_t file_read_byte = file_left < step ? file_left : step;
    size_t file_zero_byte = PGSIZE - file_read_byte;

    lock_acquire(&filesys_lock);
    aux->file = file_reopen(file);
    lock_release(&filesys_lock);
    if (aux->file == NULL) {
      free(aux);
      do_munmap(upage);
      return NULL;
    }
    aux->offset = ofs;
    aux->read_bytes = file_read_byte;
    aux->zero_bytes = file_zero_byte;
    aux->owns_file = true;

    // 페이지 할당
    if (!vm_alloc_page_with_initializer(VM_FILE, upage, writable,
                                        lazy_load_mmap, aux)) {
      lock_acquire(&filesys_lock);
      file_close(aux->file);
      lock_release(&filesys_lock);
      free(aux);
      do_munmap(upage);
      return NULL;
    }

    // 다음 페이지로
    remain -= step;         // 매핑 길이는 PGSIZE 기준으로 소모
    ofs += file_read_byte;  // 파일 오프셋은 실제 읽은 만큼만 전진
    upage += PGSIZE;
  }

  return base;
}

static bool lazy_load_mmap(struct page *page, void *aux_) {
  ASSERT(page != NULL);
  ASSERT(page->frame != NULL);

  // struct file_page *file_page = &page->file;
  // void *kva = page->frame->kva;
  struct file_page *file_page = &page->file;
  struct file_page *init = aux_;
  *file_page = *init;
  free(init);
  void *kva = page->frame->kva;

  /* 파일에서 필요한 만큼 읽기 */
  if (file_page->read_bytes > 0) {
    off_t n;
    lock_acquire(&filesys_lock);
    n = file_read_at(file_page->file, kva, file_page->read_bytes,
                     file_page->offset);
    lock_release(&filesys_lock);
    if (n != (off_t)file_page->read_bytes) {
      return false;
    }
  }

  /* 남은 공간 0으로 채우기 */
  if (file_page->zero_bytes > 0) {
    memset((uint8_t *)kva + file_page->read_bytes, 0, file_page->zero_bytes);
  }

  return true;
}

/* Do the munmap */
void do_munmap(void *addr) {
  while (1) {
    struct thread *cur = thread_current();
    struct page *page = spt_find_page(&cur->spt, addr);

    if (page == NULL) return;

    // struct load_aux *aux = (struct load_aux *)page->uninit.aux;
    // page->file.aux = aux;

    hash_delete(&cur->spt.h, &page->spt_elem);
    // file_backed_destroy(page);
    vm_dealloc_page(page);

    addr += PGSIZE;
  }
}
