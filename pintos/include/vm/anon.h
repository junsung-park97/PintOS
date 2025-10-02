#ifndef VM_ANON_H
#define VM_ANON_H
/* vm.h에서도 anon.h 참조하며 생기는 무한 참조 막기위해 제거
 * (struct anon_page 정의되기 이전에 vm.h가 참조한다는 뜻)
 */
#include <stdbool.h>  // bool
#include <stddef.h>   // size_t
struct page;
enum vm_type;

struct anon_page {
  // 미할당 상태 : SIZE_MAX or 할당 상태 : 해당 슬롯 idx
  size_t slot_idx;
};

void vm_anon_init(void);
bool anon_initializer(struct page *page, enum vm_type type, void *kva);

#endif
