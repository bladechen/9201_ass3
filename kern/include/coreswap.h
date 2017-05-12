// #include <
#ifndef _CORE_SWAP_H_
#define _CORE_SWAP_H_

#include <vm.h>

#define SWAP_FILE ".os161_coreswap"
// #define SWAP_PAGE_COUNT

enum E_SWAP_STATUS
{
    SWAP_VACANCY = 0,
    SWAP_ALLOCATING = 1,
    SWAP_INUSE = 2,

};

struct swap_page
{

    pagetable_entry* owner; // owned by user prog , if user prog, pointing to the the leaf entry of pagetable. the kernel page should not be swapped out

    off_t offset; // which location of the swap file stored in core swap, start from 0

    int swap_status; // E_SWAP_STATUS

};

void init_coreswap();
void destroy_coreswap();

int swapout_corepage(struct core_page* page);
int swapin_corepage(struct core_page* page);

#endif
