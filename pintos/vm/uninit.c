/* uninit.c: Implementation of uninitialized page.
 *
 * All of the pages are born as uninit page. When the first page fault occurs,
 * the handler chain calls uninit_initialize (page->operations.swap_in).
 * The uninit_initialize function transmutes the page into the specific page
 * object (anon, file, page_cache), by initializing the page object,and calls
 * initialization callback that passed from vm_alloc_page_with_initializer
 * function.
 * */

#include "vm/uninit.h"

#include "vm/vm.h"

static bool uninit_initialize(struct page *page, void *kva);
static void uninit_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations uninit_ops = {
    .swap_in = uninit_initialize,
    .swap_out = NULL,
    .destroy = uninit_destroy,
    .type = VM_UNINIT,
};

/* DO NOT MODIFY this function */
void uninit_new(struct page *page, void *va, vm_initializer *init,
                enum vm_type type, void *aux,
                bool (*initializer)(struct page *, enum vm_type, void *)) {
  ASSERT(page != NULL);

  *page = (struct page){.operations = &uninit_ops,
                        .va = va,
                        .frame = NULL, /* no frame for now */
                        .uninit = (struct uninit_page){
                            .init = init,
                            .type = type,
                            .aux = aux,
                            .page_initializer = initializer,
                        }};
}

/* Initalize the page on first fault */
/* 변경없이 그대로 사용합니다~~~ */
static bool uninit_initialize(struct page *page, void *kva) {
  struct uninit_page *uninit = &page->uninit;

  /* page_initializer가 페이지의 내용을 덮어쓸 수 있으므로,
   * 필요한 값들을 미리 지역 변수에 저장한다. */
  vm_initializer *init = uninit->init;
  void *aux = uninit->aux;

  /* 이 함수는 두 단계로 동작한다.
   * 1. 페이지 타입 변환: uninit 페이지를 실제 타입(anon, file)으로 바꾼다.
   *    (uninit -> page_initializer 호출)
   * 2. 데이터 로딩: 변환된 페이지에 실제 데이터를 로드
   *    (init 함수 포인터 호출)
   * 두 단계가 모두 성공해야 true를 반환
   */

  /* TODO: You may need to fix this function. */
  return uninit->page_initializer(page, uninit->type, kva) &&
         (init ? init(page, aux) : true);
}

/* Free the resources hold by uninit_page. Although most of pages are transmuted
 * to other page objects, it is possible to have uninit pages when the process
 * exit, which are never referenced during the execution.
 * PAGE will be freed by the caller. */
/* uninit_page가 보유한 리소스를 해제합니다.
 * 대부분의 페이지들은 다른 페이지 객체로 변환되지만,
 * 프로세스가 종료할 때까지 실행 중에 한 번도 참조되지 않은
 * uninit 페이지가 남아 있을 수 있습니다.
 * PAGE 자체는 호출자가 해제합니다. */
static void uninit_destroy(struct page *page) {
  struct uninit_page *uninit UNUSED = &page->uninit;
  if (uninit->aux) {
    free(uninit->aux);
    uninit->aux = NULL;
  }
}
