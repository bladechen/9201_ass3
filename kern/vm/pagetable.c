
#include <pagetable.h>
#include <hashlib.h>
#include <vm.h>
#include <lib.h>

#define ENOPTE 4
#define HASHLENGTH 8
// Global structs to define

static struct hashed_page_table *hpt = NULL;

// hashtable_size should be initialised in the init function
static int hashtable_size = 0;
static const void *emptypointer = NULL;

// Prototypes defined to avoid compiler error
static void construct_key( vaddr_t vaddr, pid_t pid , unsigned char* ptr );
static struct hpt_entry* get_free_entry( void );
static bool is_equal(vaddr_t vaddr ,pid_t pid , struct hpt_entry* current );
static void store_in_table( vaddr_t vaddr, pid_t pid, paddr_t paddr, char control, struct hpt_entry* hpt_ent );
static void set_page_zero( struct hpt_entry* current );

/*  Hash algorithm to calculate the value pair for the given key
    Note the hash's key is the virtual page address and the process id (which is what it acts on)
    This function should return an integer index into the array of the hash table entries
*/
static int hash( vaddr_t vaddr , pid_t pid )
{
    KASSERT(vaddr != 0);
    unsigned char key[HASHLENGTH];

    construct_key(vaddr, pid, key);
    int index = calculate_hash(key, HASHLENGTH, hashtable_size);

    return index;
    // return 1;
}

// this initialises the page table
void init_page_table( void )
{
    // no need to lock here
    ram_size = ram_getsize();
    KASSERT( ram_size > 0 );

    DEBUG(DB_VM, "RAM SIZE is %d\n", ram_size);
    // allocate the memory for the hashed_page_table
    hpt = (struct hashed_page_table *) kmalloc(sizeof(*hpt));
    KASSERT(hpt != NULL);

    // it should technically be round(ram_size/PAGE_SIZE) which is
    // if ram_size == 4095 then number_of_frames = 1
    int number_of_frames = ram_size/PAGE_SIZE;

    // The size of hashtable is only equal to the 2 * number of frames
    hashtable_size = 2*number_of_frames;

    // Allocate for the hpt_entries array, kmalloc will call ram_stealmem if the vm bootstrap hasnt been complete
    hpt->hpt_entry = kmalloc(hashtable_size * sizeof(struct hpt_entry));
    KASSERT(hpt->hpt_entry != NULL);

    DEBUG(DB_VM, "Hash Page Table Initialised...\n");
    // set all values hpt_entries (vaddr and paddr) to point to global free pointer and others to 0
    hpt->hpt_lock = kmalloc(sizeof(struct spinlock));
    // Initialise locks
    spinlock_init(hpt->hpt_lock);

    int i = 0;
    spinlock_acquire(hpt->hpt_lock);
    for (i = 0; i<hashtable_size; i++)
    {
        set_page_zero(&(hpt->hpt_entry[i]));
    }
    spinlock_release(hpt->hpt_lock);

#ifdef DEBUGLOAD
    // set load to zero
    hpt->load = 0;
#endif
    DEBUG(DB_VM, "Number of Page table entries = %d\nHash table Load: %2d\n", hashtable_size, hpt->load);
    unsigned long size_inbytes_pagetable = hashtable_size * sizeof(struct hpt_entry);
    DEBUG(DB_VM, "Size of Page table: %2lu\n", size_inbytes_pagetable );
}

// Helper function to construct the key for the hash function
static void construct_key( vaddr_t vaddr, pid_t pid , unsigned char* ptr )
{
    int i;
    for(i=0;i<8;i++)
    {
        if(i<4)
            ptr[i] = ( vaddr >> (i*8) ) & 0xff;
        else
            ptr[i] = ( pid >> ((i-4)*8) ) & 0xff;
    }
}

// See if there are collisions with the hash index
static bool is_colliding( vaddr_t vaddr, pid_t pid )
{
    int index = hash(vaddr,pid);
    KASSERT(spinlock_do_i_hold(hpt->hpt_lock));

    if ( hpt->hpt_entry[index].next == (struct hpt_entry *) emptypointer )
    {
        return false;
    }
    return true;
}

// WARNING no lock for this function, caller must have lock between this function
static void store_in_table( vaddr_t vaddr, pid_t pid, paddr_t paddr, char control, struct hpt_entry* hpt_ent )
{
    KASSERT(spinlock_do_i_hold(hpt->hpt_lock));
    hpt_ent->vaddr = vaddr;
    hpt_ent->paddr = paddr;
    hpt_ent->control = control;
    hpt_ent->pid = pid;
    hpt_ent->next = NULL;
}

// WARNING no lock for this function, caller must have lock between this function
static void set_page_zero( struct hpt_entry* current )
{
    KASSERT(spinlock_do_i_hold(hpt->hpt_lock));
    store_in_table( (vaddr_t) emptypointer, 0 ,(paddr_t) emptypointer, 0, current);
}

// To store an entry into the page table
bool store_entry( vaddr_t vaddr , pid_t pid, paddr_t paddr , char control )
{
    KASSERT(vaddr != (vaddr_t) emptypointer);

    // Get the page and frame numbers (upper 20 bits only)
    vaddr = vaddr & ENTRYMASK;
    paddr = paddr & ENTRYMASK;

    int index = hash(vaddr,pid);

    spinlock_acquire(hpt->hpt_lock);
    if ( !is_colliding( vaddr , pid ) )
    {
        store_in_table(vaddr, pid, paddr, control, &(hpt->hpt_entry[index]) );
        #ifdef DEBUGLOAD
        hpt->load++;
        #endif
    }
    else
    {
        // index pointer
        struct hpt_entry *current = &(hpt->hpt_entry[index]);
        // The chained pointer
        struct hpt_entry *nextchained = hpt->hpt_entry[index].next;

        // Get free entry from pool
        struct hpt_entry *free = get_free_entry();

        // TODO When there are no more free nodes
        if ( free == NULL )
        {
            spinlock_release(hpt->hpt_lock);
            // TODO this needs a *retval to be set to error no ENOMEM
            return false;
        }
        // Store in table
        store_in_table( vaddr, pid, paddr, control, free );
        // link in the next chain
        current->next = free;
        // What if free is NULL
        free->next = nextchained;
    }
    spinlock_release(hpt->hpt_lock);
    return true;
}

// Gets an entry from the pool
static struct hpt_entry* get_free_entry( void )
{
    return kmalloc(sizeof(struct hpt_entry));
}

// TODO do we need to check with the permission of the page to compare before removing?
// Remove an entry from the hash table
int remove_page_entry( vaddr_t vaddr, pid_t pid )
{
    KASSERT(vaddr != (vaddr_t) emptypointer);
    // Get the page number (upper 20 bits)
    vaddr = vaddr & ENTRYMASK;

    // Get hash index
    int index = hash(vaddr, pid);
    spinlock_acquire(hpt->hpt_lock);
    struct hpt_entry *current = &(hpt->hpt_entry[index]);
    struct hpt_entry *prev;

    // Check if the index matches the vaddr and pid
    if ( is_equal(vaddr,pid,current) )
    {
        if (current->next == NULL)
        {
            set_page_zero(current);

#ifdef DEBUGLOAD
            hpt->load--;
#endif
            spinlock_release(hpt->hpt_lock);
            return 0;
        }
        else
        {

            memcpy(current, current->next, sizeof(*current));
            current->next = current->next->next;
            kfree(current->next);
            spinlock_release(hpt->hpt_lock);
            return 0;
        }
    }
    else
    {
        prev = current;
        current = current->next;
        while( current != NULL )
        {
            // check if the vaddr and pid are the same
            // if they are then release that node to the free pool
            if( is_equal(vaddr,pid,current) )
            {
                // Redirect the pointers
                prev->next = current->next;

                // set to zeros
                // TODO this is not really necessary
                // If its in the free pool the data dosent matter
                set_page_zero(current);

                // Release node into free pool
                kfree(current);

                spinlock_release(hpt->hpt_lock);
                return 0;
            }
            prev = current;
            current = current->next;
        }
        spinlock_release(hpt->hpt_lock);
        return -1;
    }
}

// TODO what about the control bits, should we check against that? I dont think so
// Gets the physical frame address in memory
static struct hpt_entry* get_page( vaddr_t vaddr , pid_t pid )
{
    KASSERT(spinlock_do_i_hold(hpt->hpt_lock));
    KASSERT(vaddr != (vaddr_t) emptypointer);

    // Get the page number (upper 20 bits)
    vaddr = vaddr & ENTRYMASK;

    // Get hash index
    int index = hash(vaddr, pid);
    /* spinlock_acquire(hpt->hpt_lock); */
    struct hpt_entry* current = &(hpt->hpt_entry[index]);

    // Check if the index entry matches the vaddr and pid
    if ( is_equal(vaddr,pid,current) )
    {
        spinlock_release(hpt->hpt_lock);
        return current;
    }
    else
    {
        // Still have the lock
        current = current->next;
        while( current != NULL )
        {
            // check if the vaddr and pid are the same
            // if they are then return current pointer
            if( is_equal(vaddr,pid,current) )
            {
                spinlock_release(hpt->hpt_lock);
                return current;
            }
            current = current->next;
        }
    }
    // if we get here then current should be NULL
    /* spinlock_release(hpt->hpt_lock); */
    return current;
}

/* // TODO */
/* // Allocate a page and return the index */
/* struct hpt_entry* allocate_page( void ) */
/* { */
/*     return NULL; */
/* } */

// WARNING this dosent have a lock the caller should have a lock around this!!!
static bool is_equal(vaddr_t vaddr ,pid_t pid , struct hpt_entry* current )
{
    KASSERT(current != NULL);
    KASSERT(spinlock_do_i_hold(hpt->hpt_lock));
    // Fixed as vaddr is only the top 20 bits now
    return ((vaddr == current->vaddr) && (pid == current->pid));
}

// Is this entry present in the hash table already?
bool is_valid_virtual( vaddr_t vaddr , pid_t pid )
{

    // Get the page numbers (upper 20 bits)
    vaddr = vaddr & ENTRYMASK;
    KASSERT(vaddr != (vaddr_t) emptypointer);

    int index = hash(vaddr, pid);
    spinlock_acquire(hpt->hpt_lock);
    if ( hpt->hpt_entry[index].vaddr != (vaddr_t) emptypointer )
    {
        if ( hpt->hpt_entry[index].pid == pid )
        {
            spinlock_release(hpt->hpt_lock);
            return true;
        }
        else
        {
            struct hpt_entry* current = hpt->hpt_entry[index].next;
            while( current != NULL )
            {
                // check if the vaddr and pid are the same
                // if they are then return true
                if( is_equal(vaddr,pid,current) )
                {
                    spinlock_release(hpt->hpt_lock);
                    return true;
                }
            }
        }
    }
    spinlock_release(hpt->hpt_lock);
    return false;
}

/*
    These 4 functions take and entry and find out the permissions and other meta data
    of the entry
   */

bool is_valid( vaddr_t vaddr , pid_t pid )
{
    vaddr = vaddr & ENTRYMASK;
    spinlock_acquire(hpt->hpt_lock);
    struct hpt_entry *pte = get_page(vaddr, pid);

    KASSERT(pte != NULL);
    if ( (pte->control & VALIDMASK) == VALIDMASK )
    {
        spinlock_release(hpt->hpt_lock);
        return true;
    }
    spinlock_release(hpt->hpt_lock);
    return false;
}

bool is_global( vaddr_t vaddr , pid_t pid )
{
    vaddr = vaddr & ENTRYMASK;
    spinlock_acquire(hpt->hpt_lock);
    struct hpt_entry *pte = get_page(vaddr, pid);

    KASSERT(pte != NULL);
    if ( (pte->control & GLOBALMASK) == GLOBALMASK )
    {
        spinlock_release(hpt->hpt_lock);
        return true;
    }
    spinlock_release(hpt->hpt_lock);
    return false;
}

bool is_dirty( vaddr_t vaddr , pid_t pid )
{
    vaddr = vaddr & ENTRYMASK;
    spinlock_acquire(hpt->hpt_lock);
    struct hpt_entry *pte = get_page(vaddr, pid);

    KASSERT(pte != NULL);
    if ( (pte->control & DIRTYMASK) == DIRTYMASK )
    {
        spinlock_release(hpt->hpt_lock);
        return true;
    }
    spinlock_release(hpt->hpt_lock);
    return false;
}

bool is_non_cacheable( vaddr_t vaddr , pid_t pid )
{
    vaddr = vaddr & ENTRYMASK;
    spinlock_acquire(hpt->hpt_lock);
    struct hpt_entry *pte = get_page(vaddr, pid);

    KASSERT(pte != NULL);
    if ( (pte->control & NCACHEMASK) == NCACHEMASK )
    {
        spinlock_release(hpt->hpt_lock);
        return true;
    }
    spinlock_release(hpt->hpt_lock);
    return false;
}

void set_mask( vaddr_t vaddr , pid_t pid , uint32_t mask)
{
    vaddr = vaddr & ENTRYMASK;
    spinlock_acquire(hpt->hpt_lock);
    struct hpt_entry *pte = get_page(vaddr, pid);

    KASSERT(pte != NULL);
    pte->control |= mask;
    spinlock_release(hpt->hpt_lock);
}

void reset_mask( vaddr_t vaddr , pid_t pid , uint32_t mask)
{

    vaddr = vaddr & ENTRYMASK;
    spinlock_acquire(hpt->hpt_lock);
    struct hpt_entry *pte = get_page(vaddr, pid);

    KASSERT(pte != NULL);
    pte->control &= (~mask);
    spinlock_release(hpt->hpt_lock);
}
// TODO
// Struct to get the entries for the TLB
// Should return error code if not successful
int get_tlb_entry(vaddr_t vaddr, pid_t pid , uint32_t* tlb_hi, uint32_t* tlb_lo )
{

    vaddr = vaddr & ENTRYMASK;
    /* KASSERT((vaddr & (~ENTRYMASK)) == ) */
    KASSERT(tlb_hi != NULL && tlb_lo != NULL);
    spinlock_acquire(hpt->hpt_lock);
    struct hpt_entry *pte = get_page(vaddr, pid);
    if (pte == NULL)
    {
        spinlock_release(hpt->hpt_lock);
        return -1;
    }

    // Construct the hi entry for the tlb
    *tlb_hi = (pte->vaddr & ENTRYMASK);
    // Construct the lo entry for the tlb
    *tlb_lo = ((pte->paddr &ENTRYMASK ) | ((pte->control & CONTROLMASK) << 8));
    spinlock_release(hpt->hpt_lock);
    return 0;
}

