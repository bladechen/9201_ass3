// #include <
#ifndef _CORE_SWAP_H_
#define _CORE_SWAP_H_

#include <vm.h>
#include <pagetable.h>
#include <addrspace.h>

#define SWAP_FILE ".os161_coreswap"

#define SWAP_SIZE_TIMES_PHYSICAL 4 //
// #define SWAP_PAGE_COUNT

// enum E_SWAP_STATUS
// {
//     SWAP_VACANCY = 0,
//     SWAP_ALLOCATING = 1,
//     SWAP_INUSE = 2,
//
// };

struct swap_page
{


    // pagetable_entry* owner; // owned by user prog , if user prog, pointing to the the leaf entry of pagetable. the kernel page should not be swapped out

    off_t offset; // which location of the swap file stored in core swap, start from 0



    void* owner;

    struct swap_page* next_free;

};

void init_coreswap_file(void);
void init_coreswap(void);
void destroy_coreswap(void);

int swapout_frame(struct frame_entry* frame);
int swapin_frame(struct frame_entry* frame, int swap_offset);
void free_swap(int swap_offset);


#endif
