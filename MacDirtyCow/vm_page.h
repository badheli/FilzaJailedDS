#pragma once
@import Foundation;

#include <mach/vm_page_size.h>
#include <mach/vm_types.h>



#if __x86_64__
#define XNU_VM_HAS_DELAYED_PAGES        1
#define XNU_VM_HAS_LOPAGE               1
#define XNU_VM_HAS_LINEAR_PAGES_ARRAY   0
#else
#define XNU_VM_HAS_DELAYED_PAGES        0
#define XNU_VM_HAS_LOPAGE               0
#define XNU_VM_HAS_LINEAR_PAGES_ARRAY   1
#endif

#ifndef VM_MAP_STORE_USE_RB
#define VM_MAP_STORE_USE_RB
#endif

#define RB_ENTRY(type)                                                  \
struct {                                                                \
    struct type *rbe_left;          /* left element */              \
    struct type *rbe_right;         /* right element */             \
    struct type *rbe_parent;        /* parent element */            \
}

/* Macros that define a red-black tree */
#define RB_HEAD(name, type)                                             \
struct name {                                                           \
    struct type *rbh_root; /* root of the tree */                   \
}


struct _vm_map;
struct vm_map_entry;
struct vm_map_copy;
struct vm_map_header;
struct vm_map_links;
struct vm_map_store;

struct vm_map_store {
#ifdef VM_MAP_STORE_USE_RB
    RB_ENTRY(vm_map_store) entry;
#endif
};

#ifdef VM_MAP_STORE_USE_RB
RB_HEAD(rb_head, vm_map_store);
#endif

/*
 *    Type:        vm_map_entry_t [internal use only]
 *
 *    Description:
 *        A single mapping within an address map.
 *
 *    Implementation:
 *        Address map entries consist of start and end addresses,
 *        a VM object (or sub map) and offset into that object,
 *        and user-exported inheritance and protection information.
 *        Control information for virtual copy operations is also
 *        stored in the address map entry.
 *
 *    Note:
 *        vm_map_relocate_early_elem() knows about this layout,
 *        and needs to be kept in sync.
 */
struct vm_map_links {
    struct vm_map_entry     *prev;          /* previous entry */
    struct vm_map_entry     *next;          /* next entry */
    vm_map_offset_t         start;          /* start address */
    vm_map_offset_t         end;            /* end address */
};


/*
 *    Type:        struct vm_map_header
 *
 *    Description:
 *        Header for a vm_map and a vm_map_copy.
 *
 *    Note:
 *        vm_map_relocate_early_elem() knows about this layout,
 *        and needs to be kept in sync.
 */
struct vm_map_header {
    struct vm_map_links     links;          /* first, last, min, max */
    int                     nentries;       /* Number of entries */
    uint16_t                page_shift;     /* page shift */
    uint16_t                entries_pageable : 1;   /* are map entries pageable? */
    uint16_t                __padding : 15;
#ifdef VM_MAP_STORE_USE_RB
    struct rb_head          rb_head_store;
#endif /* VM_MAP_STORE_USE_RB */
};

#define VM_MAP_HDR_PAGE_SHIFT(hdr)      ((hdr)->page_shift)
#define VM_MAP_HDR_PAGE_SIZE(hdr)       (1 << VM_MAP_HDR_PAGE_SHIFT((hdr)))
#define VM_MAP_HDR_PAGE_MASK(hdr)       (VM_MAP_HDR_PAGE_SIZE((hdr)) - 1)


/*
 * in order to make the size of a vm_page_t 64 bytes (cache line size for both arm64 and x86_64)
 * we'll keep the next_m pointer packed... as long as the kernel virtual space where we allocate
 * vm_page_t's from doesn't span more then 256 Gbytes, we're safe.   There are live tests in the
 * vm_page_t array allocation and the zone init code to determine if we can safely pack and unpack
 * pointers from the 2 ends of these spaces
 */
typedef uint32_t        vm_page_packed_t;

struct vm_page_packed_queue_entry {
    vm_page_packed_t        next;          /* next element */
    vm_page_packed_t        prev;          /* previous element */
};

typedef struct vm_page_packed_queue_entry       *vm_page_queue_t;
typedef struct vm_page_packed_queue_entry       vm_page_queue_head_t;
typedef struct vm_page_packed_queue_entry       vm_page_queue_chain_t;
typedef struct vm_page_packed_queue_entry       *vm_page_queue_entry_t;

typedef vm_page_packed_t                        vm_page_object_t;

/*!
 * @typedef btref_t
 *
 * @brief
 * A backtrace ref is a compact pointer referencing a unique backtrace
 * in the centralized backtrace pool.
 */
typedef uint32_t btref_t;
#define BTREF_NULL              ((btref_t)0)

/*
 * FOOTPRINT ACCOUNTING:
 * The "memory footprint" is better described in the pmap layer.
 *
 * At the VM level, these 2 vm_map_entry_t fields are relevant:
 * iokit_mapped:
 *    For an "iokit_mapped" entry, we add the size of the entry to the
 *    footprint when the entry is entered into the map and we subtract that
 *    size when the entry is removed.  No other accounting should take place.
 *    "use_pmap" should be FALSE but is not taken into account.
 * use_pmap: (only when is_sub_map is FALSE)
 *    This indicates if we should ask the pmap layer to account for pages
 *    in this mapping.  If FALSE, we expect that another form of accounting
 *    is being used (e.g. "iokit_mapped" or the explicit accounting of
 *    non-volatile purgable memory).
 *
 * So the logic is mostly:
 * if entry->is_sub_map == TRUE
 *    anything in a submap does not count for the footprint
 * else if entry->iokit_mapped == TRUE
 *    footprint includes the entire virtual size of this entry
 * else if entry->use_pmap == FALSE
 *    tell pmap NOT to account for pages being pmap_enter()'d from this
 *    mapping (i.e. use "alternate accounting")
 * else
 *    pmap will account for pages being pmap_enter()'d from this mapping
 *    as it sees fit (only if anonymous, etc...)
 */

#define VME_ALIAS_BITS          12
#define VME_ALIAS_MASK          ((1u << VME_ALIAS_BITS) - 1)
#define VME_OFFSET_SHIFT        VME_ALIAS_BITS
#define VME_OFFSET_BITS         (64 - VME_ALIAS_BITS)
#define VME_SUBMAP_SHIFT        2
#define VME_SUBMAP_BITS         (sizeof(vm_offset_t) * 8 - VME_SUBMAP_SHIFT)

struct vm_map_entry {
    struct vm_map_links     links;                      /* links to other entries */
#define vme_prev                links.prev
#define vme_next                links.next
#define vme_start               links.start
#define vme_end                 links.end

    struct vm_map_store     store;

    union {
        vm_offset_t     vme_object_value;
        struct {
            vm_offset_t vme_atomic:1;           /* entry cannot be split/coalesced */
            vm_offset_t is_sub_map:1;           /* Is "object" a submap? */
            vm_offset_t vme_submap:VME_SUBMAP_BITS;
        };
        struct {
            uint32_t    vme_ctx_atomic : 1;
            uint32_t    vme_ctx_is_sub_map : 1;
            uint32_t    vme_context : 30;

            /**
             * If vme_kernel_object==1 && KASAN,
             * vme_object_or_delta holds the delta.
             *
             * If vme_kernel_object==1 && !KASAN,
             * vme_tag_btref holds a btref when vme_alias is equal to the "vmtaglog"
             * boot-arg.
             *
             * If vme_kernel_object==0,
             * vme_object_or_delta holds the packed vm object.
             */
            union {
                vm_page_object_t vme_object_or_delta;
                btref_t vme_tag_btref;
            };
        };
    };

    unsigned long long
    /* vm_tag_t          */ vme_alias:VME_ALIAS_BITS,   /* entry VM tag */
    /* vm_object_offset_t*/ vme_offset:VME_OFFSET_BITS, /* offset into object */

    /* boolean_t         */ is_shared:1,                /* region is shared */
    /* boolean_t         */__unused1:1,
    /* boolean_t         */in_transition:1,             /* Entry being changed */
    /* boolean_t         */ needs_wakeup:1,             /* Waiters on in_transition */
    /* behavior is not defined for submap type */
    /* vm_behavior_t     */ behavior:2,                 /* user paging behavior hint */
    /* boolean_t         */ needs_copy:1,               /* object need to be copied? */

    /* Only in task maps: */
#if defined(__arm64e__)
    /*
     * On ARM, the fourth protection bit is unused (UEXEC is x86_64 only).
     * We reuse it here to keep track of mappings that have hardware support
     * for read-only/read-write trusted paths.
     */
    /* vm_prot_t-like    */ protection:3,               /* protection code */
    /* boolean_t         */ used_for_tpro:1,
#else /* __arm64e__ */
    /* vm_prot_t-like    */protection:4,                /* protection code, bit3=UEXEC */
#endif /* __arm64e__ */

    /* vm_prot_t-like    */ max_protection:4,           /* maximum protection, bit3=UEXEC */
    /* vm_inherit_t      */ inheritance:2,              /* inheritance */

    /*
     * use_pmap is overloaded:
     * if "is_sub_map":
     *      use a nested pmap?
     * else (i.e. if object):
     *      use pmap accounting
     *      for footprint?
     */
    /* boolean_t         */ use_pmap:1,
    /* boolean_t         */ no_cache:1,                 /* should new pages be cached? */
    /* boolean_t         */ vme_permanent:1,            /* mapping can not be removed */
    /* boolean_t         */ superpage_size:1,           /* use superpages of a certain size */
    /* boolean_t         */ map_aligned:1,              /* align to map's page size */
    /*
     * zero out the wired pages of this entry
     * if is being deleted without unwiring them
     */
    /* boolean_t         */ zero_wired_pages:1,
    /* boolean_t         */ used_for_jit:1,
    /* boolean_t         */ csm_associated:1,       /* code signing monitor will validate */

    /* iokit accounting: use the virtual size rather than resident size: */
    /* boolean_t         */ iokit_acct:1,
    /* boolean_t         */ vme_resilient_codesign:1,
    /* boolean_t         */ vme_resilient_media:1,
    /* boolean_t         */ vme_xnu_user_debug:1,
    /* boolean_t         */ vme_no_copy_on_read:1,
    /* boolean_t         */ translated_allow_execute:1, /* execute in translated processes */
    /* boolean_t         */ vme_kernel_object:1;        /* vme_object is a kernel_object */

    unsigned short          wired_count;                /* can be paged if = 0 */
    unsigned short          user_wired_count;           /* for vm_wire */
};





#pragma mark - struct vm_page


#define VMP_CS_BITS 4
typedef uint8_t vm_page_q_state_t;
typedef uint8_t vm_page_specialq_t;

/*
 * The structure itself. See the block comment above for what (O) and (P) mean.
 */
struct vm_page {
    union {
        vm_page_queue_chain_t   vmp_pageq;      /* queue info for FIFO queue or free list (P) */
        struct vm_page         *vmp_snext;
    };
    vm_page_queue_chain_t           vmp_specialq;   /* anonymous pages in the special queues (P) */

    vm_page_queue_chain_t           vmp_listq;      /* all pages in same object (O) */
    vm_page_packed_t                vmp_next_m;     /* VP bucket link (O) */

    vm_page_object_t                vmp_object;     /* which object am I in (O&P) */
    vm_object_offset_t              vmp_offset;     /* offset into that object (O,P) */


    /*
     * Either the current page wire count,
     * or the local queue id (if local queues are enabled).
     *
     * See the comments at 'vm_page_queues_remove'
     * as to why this is safe to do.
     */
    union {
        uint16_t                vmp_wire_count;
        uint16_t                vmp_local_id;
    };

    /*
     * The following word of flags used to be protected by the "page queues" lock.
     * That's no longer true and what lock, if any, is needed may depend on the
     * value of vmp_q_state.
     *
     * This bitfield is kept in its own struct to prevent coalescing
     * with the next one (which C allows the compiler to do) as they
     * are under different locking domains
     */
    struct {
        vm_page_q_state_t       vmp_q_state:4;      /* which q is the page on (P) */
        vm_page_specialq_t      vmp_on_specialq:2;
        uint8_t                 vmp_lopage:1;
        uint8_t                 vmp_canonical:1;    /* this page is a canonical kernel page (immutable) */
    };
    struct {
        uint8_t                 vmp_gobbled:1;      /* page used internally (P) */
        uint8_t                 vmp_laundry:1;      /* page is being cleaned now (P)*/
        uint8_t                 vmp_no_cache:1;     /* page is not to be cached and should */
                                                    /* be reused ahead of other pages (P) */
        uint8_t                 vmp_reference:1;    /* page has been used (P) */
        uint8_t                 vmp_realtime:1;     /* page used by realtime thread (P) */
#if CONFIG_TRACK_UNMODIFIED_ANON_PAGES
        uint8_t                 vmp_unmodified_ro:1;/* Tracks if an anonymous page is modified after a decompression (O&P).*/
#else
        uint8_t                 __vmp_reserved1:1;
#endif
        uint8_t                 __vmp_reserved2:1;
        uint8_t                 __vmp_reserved3:1;
    };

    /*
     * The following word of flags is protected by the "VM object" lock.
     *
     * IMPORTANT: the "vmp_pmapped", "vmp_xpmapped" and "vmp_clustered" bits can be modified while holding the
     * VM object "shared" lock + the page lock provided through the pmap_lock_phys_page function.
     * This is done in vm_fault_enter() and the CONSUME_CLUSTERED macro.
     * It's also ok to modify them behind just the VM object "exclusive" lock.
     */
    unsigned int    vmp_busy:1,           /* page is in transit (O) */
        vmp_wanted:1,                     /* someone is waiting for page (O) */
        vmp_tabled:1,                     /* page is in VP table (O) */
        vmp_hashed:1,                     /* page is in vm_page_buckets[] (O) + the bucket lock */
    __vmp_unused : 1,
    vmp_clustered:1,                      /* page is not the faulted page (O) or (O-shared AND pmap_page) */
        vmp_pmapped:1,                    /* page has at some time been entered into a pmap (O) or */
                                          /* (O-shared AND pmap_page) */
        vmp_xpmapped:1,                   /* page has been entered with execute permission (O) or */
                                          /* (O-shared AND pmap_page) */
        vmp_wpmapped:1,                   /* page has been entered at some point into a pmap for write (O) */
        vmp_free_when_done:1,             /* page is to be freed once cleaning is completed (O) */
        vmp_absent:1,                     /* Data has been requested, but is not yet available (O) */
        vmp_error:1,                      /* Data manager was unable to provide data due to error (O) */
        vmp_dirty:1,                      /* Page must be cleaned (O) */
        vmp_cleaning:1,                   /* Page clean has begun (O) */
        vmp_precious:1,                   /* Page is precious; data must be returned even if clean (O) */
        vmp_overwriting:1,                /* Request to unlock has been made without having data. (O) */
                                          /* [See vm_fault_page_overwrite] */
        vmp_restart:1,                    /* Page was pushed higher in shadow chain by copy_call-related pagers */
                                          /* start again at top of chain */
        vmp_unusual:1,                    /* Page is absent, error, restart or page locked */
        vmp_cs_validated:VMP_CS_BITS,     /* code-signing: page was checked */
        vmp_cs_tainted:VMP_CS_BITS,       /* code-signing: page is tainted */
        vmp_cs_nx:VMP_CS_BITS,            /* code-signing: page is nx */
        vmp_reusable:1,
        vmp_written_by_kernel:1;          /* page was written by kernel (i.e. decompressed) */

#if !XNU_VM_HAS_LINEAR_PAGES_ARRAY
    /*
     * Physical number of the page
     *
     * Setting this value to or away from vm_page_fictitious_addr
     * must be done with (P) held
     */
    ppnum_t                         vmp_phys_page;
#endif /* !XNU_VM_HAS_LINEAR_PAGES_ARRAY */
};



#pragma mark - struct vm_object

typedef const void                              *vm_map_serial_t;


typedef uint16_t                vm_tag_t;

struct queue_entry {
    struct queue_entry      *next;          /* next element */
    struct queue_entry      *prev;          /* previous element */
};

typedef struct queue_entry      *queue_t;
typedef struct queue_entry      queue_head_t;
typedef struct queue_entry      queue_chain_t;
typedef struct queue_entry      *queue_entry_t;

typedef unsigned long           clock_sec_t;

typedef struct {
    uintptr_t               opaque[2] __kernel_data_semantics;
} lck_rw_t;


struct vm_page;

/*
 *    Types defined:
 *
 *    vm_object_t        Virtual memory object.
 *    vm_object_fault_info_t    Used to determine cluster size.
 */

struct vm_object_fault_info {
    int             interruptible;
    uint32_t        user_tag;
    vm_size_t       cluster_size;
    vm_behavior_t   behavior;
    vm_object_offset_t lo_offset;
    vm_object_offset_t hi_offset;
    unsigned int
    /* boolean_t */ no_cache:1,
    /* boolean_t */ stealth:1,
    /* boolean_t */ io_sync:1,
    /* boolean_t */ cs_bypass:1,
    /* boolean_t */ csm_associated:1,
    /* boolean_t */ mark_zf_absent:1,
    /* boolean_t */ batch_pmap_op:1,
    /* boolean_t */ resilient_media:1,
    /* boolean_t */ no_copy_on_read:1,
    /* boolean_t */ fi_xnu_user_debug:1,
    /* boolean_t */ fi_used_for_tpro:1,
    /* boolean_t */ fi_change_wiring:1,
    /* boolean_t */ fi_no_sleep:1,
        __vm_object_fault_info_unused_bits:19;
    int             pmap_options;
};

#define vo_size                         vo_un1.vou_size
#define vo_cache_pages_to_scan          vo_un1.vou_cache_pages_to_scan
#define vo_shadow_offset                vo_un2.vou_shadow_offset
#define vo_cache_ts                     vo_un2.vou_cache_ts
#define vo_owner                        vo_un2.vou_owner

struct vm_object {
    /*
     * on 64 bit systems we pack the pointers hung off the memq.
     * those pointers have to be able to point back to the memq.
     * the packed pointers are required to be on a 64 byte boundary
     * which means 2 things for the vm_object...  (1) the memq
     * struct has to be the first element of the structure so that
     * we can control its alignment... (2) the vm_object must be
     * aligned on a 64 byte boundary... for static vm_object's
     * this is accomplished via the 'aligned' attribute... for
     * vm_object's in the zone pool, this is accomplished by
     * rounding the size of the vm_object element to the nearest
     * 64 byte size before creating the zone.
     */
    vm_page_queue_head_t    memq;           /* Resident memory - must be first */
    lck_rw_t                Lock;           /* Synchronization */

    union {
        vm_object_size_t  vou_size;     /* Object size (only valid if internal) */
        int               vou_cache_pages_to_scan;      /* pages yet to be visited in an
                                                         * external object in cache
                                                         */
    } vo_un1;

    struct vm_page          *memq_hint;
//    os_ref_atomic_t         ref_count;        /* Number of references */
    uint32_t                    ref_count;
    unsigned int            resident_page_count;
    /* number of resident pages */
    unsigned int            wired_page_count; /* number of wired pages
                                               *  use VM_OBJECT_WIRED_PAGE_UPDATE macros to update */
    unsigned int            reusable_page_count;

    struct vm_object        *vo_copy;       /* Object that should receive
                                             * a copy of my changed pages,
                                             * for copy_delay, or just the
                                             * temporary object that
                                             * shadows this object, for
                                             * copy_call.
                                             */
    uint32_t                vo_copy_version;
    uint32_t                vo_inherit_copy_none:1,
        __vo_unused_padding:31;
    struct vm_object        *shadow;        /* My shadow */
    memory_object_t         pager;          /* Where to get data */

    union {
        vm_object_offset_t vou_shadow_offset;   /* Offset into shadow */
        clock_sec_t     vou_cache_ts;   /* age of an external object
                                         * present in cache
                                         */
        task_t          vou_owner;      /* If the object is purgeable
                                         * or has a "ledger_tag", this
                                         * is the task that owns it.
                                         */
    } vo_un2;

    vm_object_offset_t      paging_offset;  /* Offset into memory object */
    memory_object_control_t pager_control;  /* Where data comes back */

    memory_object_copy_strategy_t
        copy_strategy;                      /* How to handle data copy */

    /*
     * Some user processes (mostly VirtualMachine software) take a large
     * number of UPLs (via IOMemoryDescriptors) to wire pages in large
     * VM objects and overflow the 16-bit "activity_in_progress" counter.
     * Since we never enforced any limit there, let's give them 32 bits
     * for backwards compatibility's sake.
     */
    uint16_t                paging_in_progress;
    uint16_t                vo_size_delta;
    uint32_t                activity_in_progress;

    /* The memory object ports are
     * being used (e.g., for pagein
     * or pageout) -- don't change
     * any of these fields (i.e.,
     * don't collapse, destroy or
     * terminate)
     */

    unsigned int
    /* boolean_t array */ all_wanted:7,     /* Bit array of "want to be
                                             * awakened" notations.  See
                                             * VM_OBJECT_EVENT_* items
                                             * below */
    /* boolean_t */ pager_created:1,        /* Has pager been created? */
    /* boolean_t */ pager_initialized:1,    /* Are fields ready to use? */
    /* boolean_t */ pager_ready:1,          /* Will pager take requests? */

    /* boolean_t */ pager_trusted:1,        /* The pager for this object
                                             * is trusted. This is true for
                                             * all internal objects (backed
                                             * by the default pager)
                                             */
    /* boolean_t */ can_persist:1,          /* The kernel may keep the data
                                             * for this object (and rights
                                             * to the memory object) after
                                             * all address map references
                                             * are deallocated?
                                             */
    /* boolean_t */ internal:1,             /* Created by the kernel (and
                                             * therefore, managed by the
                                             * default memory manger)
                                             */
    /* boolean_t */ private:1,              /* magic device_pager object,
                                            * holds private pages only */
    /* boolean_t */ pageout:1,              /* pageout object. contains
                                             * private pages that refer to
                                             * a real memory object. */
    /* boolean_t */ alive:1,                /* Not yet terminated */

    /* boolean_t */ purgable:2,             /* Purgable state.  See
                                             * VM_PURGABLE_*
                                             */
    /* boolean_t */ purgeable_only_by_kernel:1,
    /* boolean_t */ purgeable_when_ripe:1,         /* Purgeable when a token
                                                    * becomes ripe.
                                                    */
    /* boolean_t */ shadowed:1,             /* Shadow may exist */
    /* boolean_t */ true_share:1,
    /* This object is mapped
     * in more than one place
     * and hence cannot be
     * coalesced */
    /* boolean_t */ terminating:1,
    /* Allows vm_object_lookup
     * and vm_object_deallocate
     * to special case their
     * behavior when they are
     * called as a result of
     * page cleaning during
     * object termination
     */
    /* boolean_t */ named:1,                /* An enforces an internal
                                             * naming convention, by
                                             * calling the right routines
                                             * for allocation and
                                             * destruction, UBC references
                                             * against the vm_object are
                                             * checked.
                                             */
    /* boolean_t */ shadow_severed:1,
    /* When a permanent object
     * backing a COW goes away
     * unexpectedly.  This bit
     * allows vm_fault to return
     * an error rather than a
     * zero filled page.
     */
    /* boolean_t */ phys_contiguous:1,
    /* Memory is wired and
     * guaranteed physically
     * contiguous.  However
     * it is not device memory
     * and obeys normal virtual
     * memory rules w.r.t pmap
     * access bits.
     */
    /* boolean_t */ nophyscache:1,
    /* When mapped at the
     * pmap level, don't allow
     * primary caching. (for
     * I/O)
     */
    /* boolean_t */ for_realtime:1,
    /* Might be needed for realtime code path */
    /* vm_object_destroy_reason_t */ no_pager_reason:3,
    /* differentiate known and unknown causes */
#if FBDP_DEBUG_OBJECT_NO_PAGER
    /* boolean_t */ fbdp_tracked:1;
#else /* FBDP_DEBUG_OBJECT_NO_PAGER */
    __object1_unused_bits:1;
#endif /* FBDP_DEBUG_OBJECT_NO_PAGER */

    queue_chain_t           cached_list;    /* Attachment point for the
                                             * list of objects cached as a
                                             * result of their can_persist
                                             * value
                                             */
    /*
     * the following fields are not protected by any locks
     * they are updated via atomic compare and swap
     */
    vm_object_offset_t      last_alloc;     /* last allocation offset */
    vm_offset_t             cow_hint;       /* last page present in     */
                                            /* shadow but not in object */
    int32_t                 sequential;     /* sequential access size */

    uint32_t                pages_created;
    uint32_t                pages_used;
    /* hold object lock when altering */
    unsigned        int
        wimg_bits:8,                /* cache WIMG bits         */
        code_signed:1,              /* pages are signed and should be
                                     *  validated; the signatures are stored
                                     *  with the pager */
        transposed:1,               /* object was transposed with another */
        mapping_in_progress:1,      /* pager being mapped/unmapped */
        phantom_isssd:1,
        volatile_empty:1,
        volatile_fault:1,
        all_reusable:1,
        blocked_access:1,
        set_cache_attr:1,
        object_is_shared_cache:1,
        purgeable_queue_type:2,
        purgeable_queue_group:3,
        io_tracking:1,
        no_tag_update:1,            /*  */
#if CONFIG_SECLUDED_MEMORY
        eligible_for_secluded:1,
        can_grab_secluded:1,
#else /* CONFIG_SECLUDED_MEMORY */
    __object3_unused_bits:2,
#endif /* CONFIG_SECLUDED_MEMORY */
#if VM_OBJECT_ACCESS_TRACKING
        access_tracking:1,
#else /* VM_OBJECT_ACCESS_TRACKING */
    __unused_access_tracking:1,
#endif /* VM_OBJECT_ACCESS_TRACKING */
    vo_ledger_tag:3,
        vo_no_footprint:1;

#if VM_OBJECT_ACCESS_TRACKING
    uint32_t        access_tracking_reads;
    uint32_t        access_tracking_writes;
#endif /* VM_OBJECT_ACCESS_TRACKING */

    uint8_t                 scan_collisions;
    uint8_t                 __object4_unused_bits[1];
    vm_tag_t                wire_tag;

#if CONFIG_PHANTOM_CACHE
    uint32_t                phantom_object_id;
#endif
#if CONFIG_IOSCHED || UPL_DEBUG
    queue_head_t            uplq;           /* List of outstanding upls */
#endif

#ifdef  VM_PIP_DEBUG
/*
 * Keep track of the stack traces for the first holders
 * of a "paging_in_progress" reference for this VM object.
 */
#define VM_PIP_DEBUG_STACK_FRAMES       25      /* depth of each stack trace */
#define VM_PIP_DEBUG_MAX_REFS           10      /* track that many references */
    struct __pip_backtrace {
        void *pip_retaddr[VM_PIP_DEBUG_STACK_FRAMES];
    } pip_holders[VM_PIP_DEBUG_MAX_REFS];
#endif  /* VM_PIP_DEBUG  */

    queue_chain_t           objq;      /* object queue - currently used for purgable queues */
    queue_chain_t           task_objq; /* objects owned by task - protected by task lock */

#if !VM_TAG_ACTIVE_UPDATE
    queue_chain_t           wired_objq;
#endif /* !VM_TAG_ACTIVE_UPDATE */

#if DEBUG
    void *purgeable_owner_bt[16];
    task_t vo_purgeable_volatilizer; /* who made it volatile? */
    void *purgeable_volatilizer_bt[16];
#endif /* DEBUG */

    /*
     * If this object is backed by anonymous memory, this represents the ID of
     * the vm_map that the memory originated from (i.e. this points backwards in
     * shadow chains). Note that an originator is present even if the object
     * hasn't been faulted into the backing pmap yet.
     */
    vm_map_serial_t vmo_provenance;
};












#pragma mark - vm_unpack_pointer


#define vm_memtag_load_tag(a)                   (a)
#define GiB(x)                  ((0ULL + (x)) << 30)
#define VM_MIN_KERNEL_ADDRESS   ((vm_address_t) (0ULL - GiB(144)))
#define VM_KERNEL_POINTER_SIGNIFICANT_BITS  38
#define VM_MIN_KERNEL_AND_KEXT_ADDRESS  VM_MIN_KERNEL_ADDRESS


/*!
 * @macro VM_PACKING_IS_BASE_RELATIVE
 *
 * @brief
 * Whether the packing scheme with those parameters will be base-relative.
 */
#define VM_PACKING_IS_BASE_RELATIVE(ns) \
    (ns##_BITS + ns##_SHIFT <= VM_KERNEL_POINTER_SIGNIFICANT_BITS)


/*!
 * @macro VM_PACKING_PARAMS
 *
 * @brief
 * Constructs a @c vm_packing_params_t structure based on the convention that
 * macros with the @c _BASE, @c _BITS and @c _SHIFT suffixes have been defined
 * to the proper values.
 */
#define VM_PACKING_PARAMS(ns) \
    (vm_packing_params_t){ \
        .vmpp_base  = ns##_BASE, \
        .vmpp_bits  = ns##_BITS, \
        .vmpp_shift = ns##_SHIFT, \
        .vmpp_base_relative = VM_PACKING_IS_BASE_RELATIVE(ns), \
    }



/*!
 * @typedef vm_packing_params_t
 *
 * @brief
 * Data structure representing the packing parameters for a given packed pointer
 * encoding.
 *
 * @discussion
 * Several data structures wish to pack their pointers on less than 64bits
 * on LP64 in order to save memory.
 *
 * Adopters are supposed to define 3 macros:
 * - @c *_BITS:  number of storage bits used for the packing,
 * - @c *_SHIFT: number of non significant low bits (expected to be 0),
 * - @c *_BASE:  the base against which to encode.
 *
 * The encoding is a no-op when @c *_BITS is equal to @c __WORDSIZE and
 * @c *_SHIFT is 0.
 *
 *
 * The convenience macro @c VM_PACKING_PARAMS can be used to create
 * a @c vm_packing_params_t structure out of those definitions.
 *
 * It is customary to declare a constant global per scheme for the sake
 * of debuggers to be able to dynamically decide how to unpack various schemes.
 *
 *
 * This uses 2 possible schemes (who both preserve @c NULL):
 *
 * 1. When the storage bits and shift are sufficiently large (strictly more than
 *    VM_KERNEL_POINTER_SIGNIFICANT_BITS), a sign-extension scheme can be used.
 *
 *    This allows to represent any kernel pointer.
 *
 * 2. Else, a base-relative scheme can be used, typical bases are:
 *
 *     - @c KERNEL_PMAP_HEAP_RANGE_START when only pointers to heap (zone)
 *       allocated objects need to be packed,
 *
 *     - @c VM_MIN_KERNEL_AND_KEXT_ADDRESS when pointers to kernel globals also
 *       need this.
 *
 *    When such an ecoding is used, @c zone_restricted_va_max() must be taught
 *    about it.
 */
typedef struct vm_packing_params {
    vm_offset_t vmpp_base;
    uint8_t     vmpp_bits;
    uint8_t     vmpp_shift;
    bool        vmpp_base_relative;
} vm_packing_params_t;


/**
 * @function vm_unpack_pointer
 *
 * @brief
 * Unpacks a pointer packed with @c vm_pack_pointer().
 *
 * @discussion
 * The convenience @c VM_UNPACK_POINTER macro allows to synthesize
 * the @c params argument.
 *
 * @param packed        The packed value to decode.
 * @param params        The encoding parameters.
 * @returns             The unpacked pointer.
 */
static inline vm_offset_t
vm_unpack_pointer(vm_offset_t packed, vm_packing_params_t params)
{
    if (!params.vmpp_base_relative) {
        intptr_t addr = (intptr_t)packed;
        addr <<= __WORDSIZE - params.vmpp_bits;
        addr >>= __WORDSIZE - params.vmpp_bits - params.vmpp_shift;
        return vm_memtag_load_tag((vm_offset_t)addr);
    }
    if (packed) {
        return vm_memtag_load_tag((packed << params.vmpp_shift) + params.vmpp_base);
    }
    return (vm_offset_t)0;
}
#define VM_UNPACK_POINTER(packed, ns) \
    vm_unpack_pointer(packed, VM_PACKING_PARAMS(ns))








#pragma mark - vm_page_unpack_ptr





#if defined(__x86_64__)
extern unsigned int     vm_clump_mask, vm_clump_shift;
#define VM_PAGE_GET_CLUMP_PNUM(pn)      ((pn) >> vm_clump_shift)
#define VM_PAGE_GET_CLUMP(m)            VM_PAGE_GET_CLUMP_PNUM(VM_PAGE_GET_PHYS_PAGE(m))
#define VM_PAGE_GET_COLOR_PNUM(pn)      (VM_PAGE_GET_CLUMP_PNUM(pn) & vm_color_mask)
#define VM_PAGE_GET_COLOR(m)            VM_PAGE_GET_COLOR_PNUM(VM_PAGE_GET_PHYS_PAGE(m))
#else
#define VM_PAGE_GET_COLOR_PNUM(pn)      ((pn) & vm_color_mask)
#define VM_PAGE_GET_COLOR(m)            VM_PAGE_GET_COLOR_PNUM(VM_PAGE_GET_PHYS_PAGE(m))
#endif

/*
 * Parameters for pointer packing
 *
 *
 * VM Pages pointers might point to:
 *
 * 1. VM_PAGE_PACKED_ALIGNED aligned kernel globals,
 *
 * 2. VM_PAGE_PACKED_ALIGNED aligned heap allocated vm pages
 *
 * 3. entries in the vm_pages array (whose entries aren't VM_PAGE_PACKED_ALIGNED
 *    aligned).
 *
 *
 * The current scheme uses 31 bits of storage and 6 bits of shift using the
 * VM_PACK_POINTER() scheme for (1-2), and packs (3) as an index within the
 * vm_pages array, setting the top bit (VM_PAGE_PACKED_FROM_ARRAY).
 *
 * This scheme gives us a reach of 128G from VM_MIN_KERNEL_AND_KEXT_ADDRESS.
 */
#define VM_VPLQ_ALIGNMENT               128
#define VM_PAGE_PACKED_PTR_ALIGNMENT    64              /* must be a power of 2 */
#define VM_PAGE_PACKED_ALIGNED          __attribute__((aligned(VM_PAGE_PACKED_PTR_ALIGNMENT)))
#define VM_PAGE_PACKED_PTR_BITS         31
#define VM_PAGE_PACKED_PTR_SHIFT        6
#define VM_PAGE_PACKED_PTR_BASE         ((uintptr_t)VM_MIN_KERNEL_AND_KEXT_ADDRESS)

#define VM_PAGE_PACKED_FROM_ARRAY       0x80000000

typedef struct vm_page  *vm_page_t;


/**
 * Internal accessor which returns the raw vm_pages pointer.
 *
 * This pointer must not be indexed directly. Use vm_page_get instead when
 * indexing into the array.
 *
 * __pure2 helps explain to the compiler that the value vm_pages is a constant.
 */
/*
__pure2
static inline struct vm_page *
vm_pages_array_internal(void)
{
    extern vm_page_t vm_page_array_beginning_addr;
    return vm_page_array_beginning_addr;
}*/

struct vm_page * vm_pages_array_internal(void);

__pure2
__attribute__((always_inline))
static inline void *
vm_far_add_ptr_internal(void *ptr, uint64_t idx, size_t elem_size,
    bool __unused idx_small)
{

    uintptr_t ptr_i = (uintptr_t)(ptr);
    uintptr_t new_ptr_i = ptr_i + (idx * elem_size);


    return __unsafe_forge_single(void *, new_ptr_i);
}

/**
 * Compute &PTR[IDX] without enforcing VM_FAR.
 *
 * In this variant, IDX will not be bounds checked.
 */
#define VM_FAR_ADD_PTR_UNBOUNDED(ptr, idx) \
    ((__typeof__((ptr))) vm_far_add_ptr_internal( \
            (ptr), (idx), sizeof(__typeof__(*(ptr))), sizeof((idx)) <= 4))


/**
 * Get a pointer to page at index i.
 *
 * This getter is the only legal way to index into the vm_pages array.
 */
__pure2
static inline vm_page_t
vm_page_get(uint32_t i)
{
    return VM_FAR_ADD_PTR_UNBOUNDED(vm_pages_array_internal(), i);
}

static inline uintptr_t
vm_page_unpack_ptr(uintptr_t p)
{
    if (p >= VM_PAGE_PACKED_FROM_ARRAY) {
        p &= ~VM_PAGE_PACKED_FROM_ARRAY;
        return (uintptr_t)vm_page_get((uint32_t)p);
    }

    return VM_UNPACK_POINTER(p, VM_PAGE_PACKED_PTR);
}

#pragma MARK - vm_page_queue_iterate

#define VM_PAGE_UNPACK_PTR(p)   vm_page_unpack_ptr((uintptr_t)(p))

/*
 *    Macro:    vm_page_queue_next
 *    Function:
 *        Returns the entry after an item in the queue.
 *    Header:
 *        uintpr_t vm_page_queue_next(qc)
 *            vm_page_queue_t qc;
 */
#define vm_page_queue_next(qc)          (VM_PAGE_UNPACK_PTR((qc)->next))

/*
 *    Macro:    vm_page_queue_end
 *    Function:
 *    Tests whether a new entry is really the end of
 *        the queue.
 *    Header:
 *        boolean_t vm_page_queue_end(q, qe)
 *            vm_page_queue_t q;
 *            vm_page_queue_entry_t qe;
 */
#define vm_page_queue_end(q, qe)        ((q) == (qe))

/*
 *    Macro:    vm_page_queue_first
 *    Function:
 *        Returns the first entry in the queue,
 *    Header:
 *        uintpr_t vm_page_queue_first(q)
 *            vm_page_queue_t q;    \* IN *\
 */
#define vm_page_queue_first(q)          (VM_PAGE_UNPACK_PTR((q)->next))

/*
 *    Macro:    vm_page_queue_iterate
 *    Function:
 *        iterate over each item in a vm_page queue.
 *        Generates a 'for' loop, setting elt to
 *        each item in turn (by reference).
 *    Header:
 *        vm_page_queue_iterate(q, elt, field)
 *            queue_t q;
 *            vm_page_t elt;
 *            <field> is the chain field in vm_page_t
 */
#define vm_page_queue_iterate(head, elt, field)                       \
    for ((elt) = (vm_page_t)vm_page_queue_first(head);            \
        !vm_page_queue_end((head), (vm_page_queue_entry_t)(elt)); \
        (elt) = (vm_page_t)vm_page_queue_next(&(elt)->field))     \
