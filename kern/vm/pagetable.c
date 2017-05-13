
#include <pagetable.h>
#include <vm.h>
#include <lib.h>

#define ENOPTE 4
// Global structs to define

static struct hashed_page_table *hpt = NULL;
static int hashtable_size = 0;
static const void *emptypointer = NULL;

/*  Hash algorithm to calculate the value pair for the given key
    Note the hash's key is the virtual page address (which is what it acts on)
    This function should return an integer index into the array of the hash table entries

    I think this should be static as its internal the the page table...
*/
static int hash( vaddr_t vaddr , pid_t pid )
{
    (void) pid;
    if ( vaddr == 0 )
        return -1;
    return 1;
}

// this initialises the page table
void init_page_table( void )
{
    // no need to lock here
    ram_size = ram_getsize();
    KASSERT( ram_size > 0 );

    // allocate the memory for the hashed_page_table 
    hpt = (struct hashed_page_table *) kmalloc(sizeof(*hpt));

    int number_of_frames = ram_size/PAGE_SIZE;

    // 2 x ram_size
    hashtable_size = 2*number_of_frames;
    // Allocate for the hpt_entries array, kmalloc will call ram_stealmem if the vm bootstrap hasnt been complete
    hpt->hpt_entry = kmalloc(hashtable_size * sizeof(struct hpt_entry));

    // set all values hpt_entries (vaddr and paddr) to point to global free pointer
    int i = 0;
    for (i = 0; i<hashtable_size; i++)
    {
        // Virtual addrs are nulls
        hpt->hpt_entry[i].vaddr = (vaddr_t) emptypointer;
        // Frames are nulls
        hpt->hpt_entry[i].paddr = (paddr_t) emptypointer;
        // Next points to null
        hpt->hpt_entry[i].next = (struct hpt_entry *) emptypointer;
    }

#ifdef DEBUGLOAD
    // set load to zero
    hpt->load = 0;
#endif
}

// See if there are collisions with the hash index
static bool is_colliding( vaddr_t vaddr , pid_t pid )
{
    int index = hash(vaddr,pid);
    if ( hpt->hpt_entry[index].next == NULL )
        return false;
    return true;
}

static void store_in_table ( vaddr_t vaddr, pid_t pid, paddr_t paddr, int index )
{
    hpt->hpt_entry[index].vaddr = vaddr;
    hpt->hpt_entry[index].paddr = paddr;
    hpt->hpt_entry[index].pid = pid;
    hpt->hpt_entry[index].next = NULL;
}
// To store an entry into the page table
void store_entry( paddr_t paddr, vaddr_t vaddr , pid_t pid )
{
    int index = hash(vaddr,pid);
    KASSERT ( index > -1 );

    if ( !is_colliding( vaddr , pid ) )
    {
        store_in_table (vaddr, pid, paddr, index );
    }
    else
    {
        struct hpt_entry *current = hpt->hpt_entry[index].next;
        (void) current;
    }
}

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
    int index = hash(vaddr, pid );
    (void) index;
    (void) pid;
    return false;
}

/* 
    These 3 functions take and entry and find out the permissions and other meta data
    of the entry
   */
bool is_global( const struct hpt_entry* pte )
{
    KASSERT(pte != NULL);
    if ( (pte->paddr & GLOBALMASK) == GLOBALMASK )
        return true;
    else
        return false;
}
bool is_dirty( const  struct hpt_entry* pte )
{
    KASSERT(pte != NULL);
    if ( (pte->paddr & DIRTYMASK) == DIRTYMASK )
        return true;
    else
        return false;

}
bool is_non_cacheable( const struct hpt_entry* pte )
{
    KASSERT(pte != NULL);
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
    int index = hash(pte->vaddr, pte->pid);
    (void) index;
    return -1;
}

