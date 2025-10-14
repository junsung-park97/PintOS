/* vm.c: Generic interface for virtual memory objects. */

#include "vm/vm.h"

#include "threads/malloc.h"
#include "threads/mmu.h"
#include "userprog/process.h"
#include "vm/inspect.h"

#define STACK_LIMIT (1 << 20)

static struct list frame_table;
static struct lock frame_lock;

/* 해시 테이블 */
static uint64_t spt_hash(const struct hash_elem *e, void *aux) {
  const struct page *p = hash_entry(e, struct page, spt_elem);
  return hash_bytes(&p->va, sizeof p->va);
}

/* 해시 비교 함수 */
static bool spt_less(const struct hash_elem *a, const struct hash_elem *b,
                     void *aux) {
  const struct page *pa = hash_entry(a, struct page, spt_elem);
  const struct page *pb = hash_entry(b, struct page, spt_elem);
  return pa->va < pb->va;
}

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void) {
  vm_anon_init();
  vm_file_init();
#ifdef EFILESYS /* For project 4 */
  pagecache_init();
#endif
  register_inspect_intr();
  /* DO NOT MODIFY UPPER LINES. */
  /* TODO: Your code goes here. */
  list_init(&frame_table);
  lock_init(&frame_lock);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type page_get_type(struct page *page) {
  int ty = VM_TYPE(page->operations->type);
  switch (ty) {
    case VM_UNINIT:
      return VM_TYPE(page->uninit.type);
    default:
      return ty;
  }
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);
void spt_destructor(struct hash_elem *e, void *aux);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage,
                                    bool writable, vm_initializer *init,
                                    void *aux) {
  ASSERT(VM_TYPE(type) != VM_UNINIT)

  struct supplemental_page_table *spt = &thread_current()->spt;
  // 현재 스레드의 spt에 대한 포인터를 얻는다.

  /* Check wheter the upage is already occupied or not. */
  upage = pg_round_down(upage);
  if (spt_find_page(spt, upage) == NULL) {
    // upage가 이미 사용 중인지 확인한다.

    struct page *page = calloc(1, sizeof(struct page));
    // 새로운 페이지 구조체를 동적으로 할당
    if (page == NULL) {
      goto err;
    }

    page->frame = NULL;

    bool (*initializer)(struct page *, enum vm_type, void *);
    // 가상 메모리 타입에 따라 초기화되는 함수 포인터를 설정
    switch (VM_TYPE(type)) {
      case VM_ANON:
        initializer = anon_initializer;
        break;

      case VM_FILE:
        initializer = file_backed_initializer;
        break;

      default:
        free(page);
        goto err;
    }

    uninit_new(page, upage, init, type, aux, initializer);
    // uninit_new 함수를 사용하여 페이지를 초기화한다.
    page->writable = writable;

    if (!spt_insert_page(spt, page)) {
      // 페이지를 보조 페이지 테이블에 삽입한다.
      free(page);
      goto err;
    }
    return true;
  }
err:
  return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *spt_find_page(struct supplemental_page_table *spt UNUSED,
                           void *va UNUSED) {
  if (!spt) return NULL;
  struct page key;
  key.va = pg_round_down(va);
  struct hash_elem *e = hash_find(&spt->h, &key.spt_elem);
  return e ? hash_entry(e, struct page, spt_elem) : NULL;
}

/* Insert PAGE into spt with validation. */
bool spt_insert_page(struct supplemental_page_table *spt UNUSED,
                     struct page *page UNUSED) {
  ASSERT(page != NULL);
  ASSERT(page->operations != NULL);
  page->va = pg_round_down(page->va);
  struct hash_elem *old = hash_insert(&spt->h, &page->spt_elem);
  return old == NULL;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page) {
  vm_dealloc_page(page);
}

/* Get the struct frame, that will be evicted. */
static struct frame *vm_get_victim(void) {
  // struct frame *victim = NULL;
  /* TODO: The policy for eviction is up to you. */
  ASSERT(!list_empty(&frame_table));
  struct list_elem *e = list_pop_front(&frame_table);
  // return victim;
  return list_entry(e, struct frame, elem);
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *vm_evict_frame(void) {
  struct frame *victim UNUSED = vm_get_victim();
  /* TODO: swap out the victim and return the evicted frame. */
  if (victim == NULL) return NULL;
  struct page *page = victim->page;
  ASSERT(page != NULL);

  if (!swap_out(page)) {
    list_push_back(&frame_table, &victim->elem);
    return NULL;
  }

  page->frame = NULL;
  victim->page = NULL;
  return victim;
}

/* palloc()으로 프레임(frame)을 획득한다. 사용 가능한 페이지가 없으면,
 * 페이지를 축출(evict)하고 반환한다. 이 함수는 항상 유효한 주소를 반환한다.
 * 즉, 사용자 풀(user pool) 메모리가 가득 찬 경우, 이 함수는 프레임을
 * 축출하여 사용 가능한 메모리 공간을 확보한다. */

static struct frame *vm_get_frame(void) {
  struct frame *frame = NULL;
  /* TODO: Fill this function. */

  lock_acquire(&frame_lock);

  // 물리페이지 획득
  void *kernal_va = palloc_get_page(PAL_USER);
  if (kernal_va != NULL) {
    // 프레임구조체 생성 및 초기화
    frame = malloc(sizeof(struct frame));
    if (frame == NULL) PANIC("to do");
    frame->kva = kernal_va;
    frame->page = NULL;
    list_push_back(&frame_table, &frame->elem);

  } else {
    frame = vm_evict_frame();
    if (frame == NULL) PANIC("to do");
    frame->page = NULL;
    list_push_back(&frame_table, &frame->elem);
  }

  lock_release(&frame_lock);
  return frame;
}

/* Growing the stack. */
static void vm_stack_growth(void *addr) {
  // int page_addr = pg_round_down(addr);

  // if (USER_STACK - page_addr > STACK_LIMIT) {
  //   return;
  // }

  if (vm_alloc_page(VM_ANON, addr, true) == false) {
    return;
  }

  // if (vm_claim_page(page_addr) == false) {
  //   return;
  // }
}

/* Handle the fault on write_protected page */
static bool vm_handle_wp(struct page *page UNUSED) {}

/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f, void *addr, bool user,
                         bool write, bool not_present) {
  // 보호 위반은 skip
  if (!not_present) return false;

  // 커널 주소/NULL은 거부
  if (addr == NULL || is_kernel_vaddr(addr)) return false;

  void *upage = pg_round_down(addr);

  // SPT에서 해당 페이지 찾기 (load_segment 때 등록된 uninit/file 페이지)
  struct supplemental_page_table *spt = &thread_current()->spt;
  struct page *page = spt_find_page(spt, upage);

  if (page == NULL) {
    void *rsp_stack = user ? f->rsp : thread_current()->user_rsp;
    if (rsp_stack == NULL) {
      return false;
    }
    // void *rsp_stack =
    //     is_kernel_vaddr(f->rsp) ? thread_current()->user_rsp : f->rsp;

    if (addr >= USER_STACK || addr < rsp_stack - 8) {
      return false;
    }

    if (addr < USER_STACK - (1 << 20)) {  // 1MB 리미트
      return false;
    }

    vm_stack_growth(upage);
    page = spt_find_page(spt, upage);
    if (page == NULL) {
      return false;
    }
  }

  if (write && !page->writable) return false;

  return vm_do_claim_page(page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page) {
  destroy(page);
  free(page);
}

/* Claim the page that allocate on VA. */
bool vm_claim_page(void *va UNUSED) {
  struct page *page = NULL;
  va = pg_round_down(va);
  page = spt_find_page(&thread_current()->spt, va);
  if (!page) return false;
  return vm_do_claim_page(page);
}

void vm_free_frame(struct frame *frame) {
  ASSERT(frame != NULL);
  ASSERT(frame->page == NULL);
  palloc_free_page(frame->kva);
  free(frame);
}

/* Claim the PAGE and set up the mmu. */
static bool vm_do_claim_page(struct page *page) {
  struct frame *frame = vm_get_frame();

  /* Set links */
  frame->page = page;
  page->frame = frame;

  /* TODO: Insert page table entry to map page's VA to frame's PA. */
  struct thread *cur = thread_current();
  if (!pml4_set_page(cur->pml4, page->va, frame->kva, page->writable)) {
    frame->page = NULL;
    page->frame = NULL;
    vm_free_frame(frame);
    return false;
  }

  if (!swap_in(page, frame->kva)) {
    pml4_clear_page(cur->pml4, page->va);
    frame->page = NULL;
    page->frame = NULL;
    vm_free_frame(frame);
    return false;
  }
  return true;
}

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED) {
  hash_init(&spt->h, spt_hash, spt_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
                                  struct supplemental_page_table *src UNUSED) {
  struct hash_iterator iterator;
  struct page *parent_page;
  hash_first(&iterator, &src->h);
  while (hash_next(&iterator)) {
    parent_page = hash_entry(hash_cur(&iterator), struct page, spt_elem);

    enum vm_type type = parent_page->operations->type;
    if (type == VM_UNINIT) {
      type = parent_page->uninit.type;
    }

    void *upage = pg_round_down(parent_page->va);
    bool writable = parent_page->writable;

    if (parent_page->operations->type == VM_UNINIT) {
      vm_initializer *init = parent_page->uninit.init;
      void *parent_aux = parent_page->uninit.aux;
      void *child_aux = NULL;

      if (type == VM_FILE && parent_aux) {
        child_aux = malloc(sizeof(struct load_aux));
        if (child_aux == NULL) {
          supplemental_page_table_kill(dst);
          return false;
        }
        memcpy(child_aux, parent_aux, sizeof(struct load_aux));

        struct file *parent_file = ((struct load_aux *)parent_aux)->file;
        struct file *child_file = file_reopen(parent_file);
        if (child_file == NULL) {
          free(child_aux);
          supplemental_page_table_kill(dst);
          return false;
        }
        ((struct load_aux *)child_aux)->file = child_file;
      }

      if (!vm_alloc_page_with_initializer(type, upage, writable, init,
                                          child_aux)) {
        if (child_aux) {
          if (type == VM_FILE) file_close(((struct load_aux *)child_aux)->file);
          free(child_aux);
        }
        supplemental_page_table_kill(dst);
        return false;
      }
    } else {
      // ANON 또는 FILE 페이지 처리
      if (!vm_alloc_page(type, upage, writable)) {
        supplemental_page_table_kill(dst);
        return false;
      }
    }
    // 부모 페이지가 프레임에 있다면 자식 페이지도 할당받고 내용 복사
    if (parent_page->frame != NULL) {
      // 자식 물리프레임 할당 및 매핑
      if (!vm_claim_page(upage)) {
        supplemental_page_table_kill(dst);
        return false;
      }
      struct page *child_page = spt_find_page(dst, upage);
      if (child_page) {
        memcpy(child_page->frame->kva, parent_page->frame->kva, PGSIZE);
      } else {
        supplemental_page_table_kill(dst);
        return false;
      }
    }
  }
  return true;
}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED) {
  /* TODO: Destroy all the supplemental_page_table hold by thread and
   * TODO: writeback all the modified contents to the storage. */
  hash_destroy(&spt->h, spt_destructor);
}

void spt_destructor(struct hash_elem *e, void *aux) {
  // TODO: hash_elem을 struct page로 변환
  // TODO: 페이지 정리
  struct page *page = hash_entry(e, struct page, spt_elem);
  vm_dealloc_page(page);
}
