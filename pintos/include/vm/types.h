// vm/types.h
#ifndef VM_TYPES_H
#define VM_TYPES_H

/* 공용 VM 타입 열거형 — 여러 헤더가 이 정의를 공유 */
enum vm_type {
  VM_UNINIT = 0,
  VM_ANON = 1,
  VM_FILE = 2,
  VM_PAGE_CACHE = 3,

  VM_MARKER_0 = (1 << 3),
  VM_MARKER_1 = (1 << 4),

  VM_MARKER_END = (1 << 31),
};

/* 타입 마스크 매크로 (원래 vm.h에 있던 것) */
#define VM_TYPE(type) ((type) & 7)

#endif /* VM_TYPES_H */
