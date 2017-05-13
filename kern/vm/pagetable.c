
#include <pagetable.h>

#define ENOPTE -1

// Global structs to define

static struct hashed_page_table *hpt = NULL;
static unsigned int hashtable_size = 0;
static void * emptypointer = NULL;

/*  Hash algorithm to calculate the value pair for the given key
    Note the hash's key is the virtual page address (which is what it acts on)
    This function should return an integer index into the array of the hash table entries

    I think this should be static as its internal the the page table...
*/
static int hash( vaddr_t vaddr )
{
   if ( vaddr == 0 )
       return -1;

    return 1;
}

// helper function to set the pointer to a global free pointer
// SHOULD ONLY BE USED AFTER emptypointer has been inited!!!
static void set_pointer_free(void * addr)
{
    (void) addr;
    addr = emptypointer;
}

// this initialises the page table
void init_page_table( void )
{
    // no need to lock here
    
    // Set global free pointer to something

    // allocate the memory for the hashed_page_table which is fixed to 
    // 2 x ram_size
    hashtable_size = 2*ram_size; 

    // set all values hpt_entries (vaddr and paddr) to point to global free pointer
    uint32_t i = 0;
    for (i = 0; i<hashtable_size; i++)
    {
        set_pointer_free( &(hpt->hpt_entry[i].vaddr) );
        set_pointer_free( &(hpt->hpt_entry[i].paddr) );
    }
    // set load to zero
    hpt->load = 0;
}

// To store an entry into the page table
void store_entry( paddr_t paddr, vaddr_t vaddr , pid_t pid );

// Remove an entry from the hash table
void remove_page_entry( vaddr_t vaddr, pid_t pid );

// Gets the physical frame address in memory
paddr_t get_frame( vaddr_t vaddr , pid_t pid );

// Allocate a page and return the index 
struct hpt_entry * allocate_page( int page_num );

// Is this entry present in the hash table already?
// O(1) to find out
bool is_valid_virtual( vaddr_t vaddr , pid_t pid , int *retval )
{
    if ( vaddr == 0 )
    {
        *retval = ENOPTE;
        return false;
    }
    int index = hash(vaddr);
    (void) index;
    (void) pid;
    return false;
}

/* 
    These 3 functions take and entry and find out the permissions and other meta data
    of the entry
   */
bool is_global( const struct hpt_entry* pte, int *retval )
{
    if ( pte == NULL )
    {
        *retval = ENOPTE;
        return false;
    }
    if ( (pte->paddr & GLOBALMASK) == GLOBALMASK )
        return true;
    else
        return false;
}
bool is_dirty( const  struct hpt_entry* pte , int *retval )
{
     if ( pte == NULL )
    {
        *retval = ENOPTE;
        return false;
    }
    if ( (pte->paddr & DIRTYMASK) == DIRTYMASK )
        return true;
    else
        return false;

}
bool is_non_cacheable( const struct hpt_entry* pte , int *retval )
{
    if ( pte == NULL )
    {
        *retval = ENOPTE;
        return false;
    }
    if ( (pte->paddr & NCACHEMASK) == NCACHEMASK )
        return true;
    else
        return false;

}
// Struct to get the entries for the TLB
// Should return error code if not successful
int get_tlb_entry( struct hpt_entry* pte, int* tlb_hi, int* tlb_lo )
{
    (void) tlb_hi;
    (void) tlb_lo;

    if ( pte == NULL )
        return -1;
    int index = hash(pte->vaddr);
    (void) index;
    return -1;
}

