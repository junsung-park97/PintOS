/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */
#include "vm/anon.h"

#include "devices/disk.h"
#include "lib/kernel/bitmap.h"
#include "threads/mmu.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "vm/vm.h"

/* DO NOT MODIFY BELOW LINE */
static bool anon_swap_in(struct page *page, void *kva);
static bool anon_swap_out(struct page *page);
static void anon_destroy(struct page *page);

// 전역 변수
static struct bitmap *swap_table;
static struct disk *swap_disk;

// 비트 마스킹용 락
struct lock swap_lock;
static const size_t SECTORS_PER_SLOT = PGSIZE / DISK_SECTOR_SIZE;

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
    .swap_in = anon_swap_in,
    .swap_out = anon_swap_out,
    .destroy = anon_destroy,
    .type = VM_ANON,
};

/* 익명 페이지를 위한 데이터를 초기화 합니다. */
void vm_anon_init(void) {
  /* swap_disk를 설정하세요 */
  swap_disk = disk_get(1, 1);
  if (swap_disk == NULL) {
    return;
  }

  disk_sector_t swap_dsize = disk_size(swap_disk);
  size_t swap_slot_size = PGSIZE / DISK_SECTOR_SIZE;
  size_t slot_count = swap_dsize / swap_slot_size;

  swap_table = bitmap_create(slot_count);
  if (swap_table == NULL) {
    return;
  }

  // void라 리턴없고 비트맵 설정 후 락 초기화
  bitmap_set_all(swap_table, false);
  lock_init(&swap_lock);
}

/* anon_page를 초기화 합니다. */
bool anon_initializer(struct page *page, enum vm_type type, void *kva) {
  /* 핸들러를 설정합니다. */
  page->operations = &anon_ops;
  page->anon.slot_idx = SIZE_MAX;
  return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool anon_swap_in(struct page *page, void *kva) {
  struct anon_page *anon_page = &page->anon;
  size_t slot = anon_page->slot_idx;

  if (slot == SIZE_MAX) return false;
  if (swap_disk == NULL) return false;

  lock_acquire(&swap_lock);
  for (size_t i = 0; i < SECTORS_PER_SLOT; i++) {
    disk_read(swap_disk, slot * SECTORS_PER_SLOT + i,
              (uint8_t *)kva + DISK_SECTOR_SIZE * i);
  }
  bitmap_reset(swap_table, slot);
  anon_page->slot_idx = SIZE_MAX;
  lock_release(&swap_lock);
  return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool anon_swap_out(struct page *page) {
  struct anon_page *anon = &page->anon;
  struct frame *frame = page->frame;
  if (frame == NULL) return false;
  if (swap_disk == NULL) return false;

  lock_acquire(&swap_lock);
  size_t slot = anon->slot_idx;
  if (slot == SIZE_MAX) {
    slot = bitmap_scan_and_flip(swap_table, 0, 1, false);
    if (slot == BITMAP_ERROR) {
      lock_release(&swap_lock);
      return false;
    }
    anon->slot_idx = slot;
  }

  for (size_t i = 0; i < SECTORS_PER_SLOT; i++) {
    disk_write(swap_disk, slot * SECTORS_PER_SLOT + i,
               (uint8_t *)frame->kva + DISK_SECTOR_SIZE * i);
  }
  lock_release(&swap_lock);
  return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void anon_destroy(struct page *page) {
  struct anon_page *ap = &page->anon;

  /* 1) 스왑 슬롯 해제: 프레임 유무와 무관하게, 슬롯이 있으면 해제 */
  if (ap->slot_idx != SIZE_MAX) {
    lock_acquire(&swap_lock);
    bitmap_reset(swap_table, ap->slot_idx);
    lock_release(&swap_lock);
    ap->slot_idx = SIZE_MAX;
  }

  /* 2) 프레임 반납: 매핑 해제 → 연결 해제 → 프레임 free */
  struct frame *frame = page->frame;
  if (frame != NULL) {
    struct thread *owner = page->owner;
    if (owner && owner->pml4) {
      pml4_clear_page(owner->pml4, page->va);
    }
    frame->page = NULL;
    page->frame = NULL;
    vm_free_frame(frame);
  }
}
