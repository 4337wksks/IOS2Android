/* Process information is obtained from Android. Mach task/host names below
 * identify this process; Linux does not provide Darwin exception-port IPC. */
static uint32_t self_task_port=0x1001;
static const uint32_t self_host_port=0x1002;
static const unsigned char guest_ndr_record[8]={0,0,0,0,1,0,0,0};
static uint32_t guest_host_self(void) { return self_host_port; }
static uint32_t guest_mach_thread_self(void) { return 0x80000000U|(uint32_t)current_thread_record()->tid; }

static uint64_t memory_field(const char *name) {
    FILE *file=fopen("/proc/meminfo","r");
    if (!file) return 0;
    char line[256],key[80];
    unsigned long long size;
    uint64_t result=0;
    while (fgets(line,sizeof(line),file)) {
        if (sscanf(line,"%79[^:]: %llu",key,&size)==2 && !strcmp(name,key)) { result=(uint64_t)size*1024; break; }
    }
    fclose(file);
    return result;
}

static int guest_host_info(uint32_t host,int flavor,int32_t *output,uint32_t *count) {
    if (host!=self_host_port || !output || !count || flavor!=1 || *count<12) return 4;
    int32_t online=(int32_t)sysconf(_SC_NPROCESSORS_ONLN),maximum=(int32_t)sysconf(_SC_NPROCESSORS_CONF);
    uint64_t memory=memory_field("MemTotal");
    int32_t values[12]={maximum,online,(int32_t)(memory>INT32_MAX ? INT32_MAX : memory),0x0100000c,0,0,online,maximum,online,maximum,0,0};
    memcpy(values+10,&memory,sizeof(memory));
    memcpy(output,values,sizeof(values)); *count=12;
    return 0;
}

static int guest_host_statistics(uint32_t host,int flavor,uint32_t *output,uint32_t *count) {
    if (host!=self_host_port || flavor!=2 || !output || !count || *count<15) return 4;
    uint64_t page=(uint64_t)sysconf(_SC_PAGESIZE);
    uint32_t values[15]={0};
    values[0]=(uint32_t)(memory_field("MemFree")/page);
    values[1]=(uint32_t)(memory_field("Active")/page);
    values[2]=(uint32_t)(memory_field("Inactive")/page);
    values[3]=(uint32_t)(memory_field("Unevictable")/page);
    values[12]=(uint32_t)(memory_field("SReclaimable")/page);
    memcpy(output,values,sizeof(values)); *count=15;
    return 0;
}

static int guest_task_info(uint32_t task,int flavor,void *output,uint32_t *count) {
    if (task!=self_task_port || flavor!=18 || !output || !count || *count<10) return 4;
    FILE *file=fopen("/proc/self/statm","r");
    if (!file) return 5;
    unsigned long long virtual_pages=0,resident_pages=0;
    int read=fscanf(file,"%llu %llu",&virtual_pages,&resident_pages);
    fclose(file);
    if (read!=2) return 5;
    struct rusage usage;
    if (getrusage(RUSAGE_SELF,&usage)) return 5;
    uint64_t virtual_bytes=virtual_pages*(uint64_t)sysconf(_SC_PAGESIZE),resident_bytes=resident_pages*(uint64_t)sysconf(_SC_PAGESIZE);
    /* task_basic_info_64_data_t is packed to four-byte alignment on Darwin. */
    unsigned char values[40]={0};
    memcpy(values+4,&virtual_bytes,8); memcpy(values+12,&resident_bytes,8);
    int32_t times[4]={(int32_t)usage.ru_utime.tv_sec,(int32_t)usage.ru_utime.tv_usec,(int32_t)usage.ru_stime.tv_sec,(int32_t)usage.ru_stime.tv_usec};
    memcpy(values+20,times,sizeof(times));
    int32_t policy=1;  
    memcpy(values+36,&policy,4);
    memcpy(output,values,sizeof(values)); *count=10;
    return 0;
}

static int guest_vm_read(uint32_t task,Word source,size_t length,Word destination,size_t *written) {
    if (task!=self_task_port || !written) return 4;
    *written=0;
    if (!length) return 0;
    if (length>SSIZE_MAX || source>UINTPTR_MAX-length || destination>UINTPTR_MAX-length) return 1;
    struct iovec local={(void *)destination,length},remote={(void *)source,length};
    ssize_t copied=process_vm_readv(getpid(),&local,1,&remote,1,0);
    if (copied<0) return errno==EFAULT ? 1 : 5;
    *written=(size_t)copied;
    return (size_t)copied==length ? 0 : 1;
}

static int guest_task_threads(uint32_t task,uint32_t **threads,uint32_t *count) {
    if (task!=self_task_port || !threads || !count) return 4;
    DIR *directory=opendir("/proc/self/task");
    if (!directory) return 5;
    uint32_t *names=NULL; size_t used=0,capacity=0;
    struct dirent *entry;
    while ((entry=readdir(directory))) {
        char *end=NULL;
        unsigned long tid=strtoul(entry->d_name,&end,10);
        if (!tid || *end || tid>0x7fffffffUL) continue;
        if (used==capacity) {
            size_t next=capacity ? capacity*2 : 32;
            uint32_t *grown=realloc(names,next*sizeof(*names));
            if (!grown) { closedir(directory); free(names); return 3; }
            names=grown; capacity=next;
        }
        names[used++]=0x80000000U|(uint32_t)tid;
    }
    closedir(directory);
    *threads=names; *count=(uint32_t)used;
    return 0;
}

static int guest_thread_info(uint32_t thread,int flavor,void *output,uint32_t *count) {
    if (!(thread&0x80000000U) || !output || !count) return 4;
    if (flavor==4 && *count>=6) {
        uint64_t info[3]={thread&0x7fffffffU,(uint64_t)guest_thread_from_mach(thread),0};
        memcpy(output,info,sizeof(info)); *count=6;
        return 0;
    }
    return 46;  
}
static int guest_thread_state(uint32_t thread,int flavor,void *output,uint32_t *count) { return 46; }
static int guest_thread_control(uint32_t thread) { return 46; }
static int guest_port_allocate(uint32_t task,int right,uint32_t *name) {
    if (name) *name=0;
    LOG("Mach port allocation unavailable on Android (right=%d)",right);
    return task==self_task_port ? 46 : 4;
}
static int guest_port_insert(uint32_t task,uint32_t name,uint32_t poly,int type) { return 46; }
static int guest_port_deallocate(uint32_t task,uint32_t name) {
    if (task!=self_task_port) return 4;
    return name==self_task_port || name==self_host_port || (name&0x80000000U) ? 0 : 15;
}
static int guest_exception_set(uint32_t task,uint32_t mask,uint32_t port,int behavior,int flavor) { return 46; }
static int guest_exception_swap(uint32_t task,uint32_t mask,uint32_t port,int behavior,int flavor,uint32_t *masks,uint32_t *count,uint32_t *ports,int *behaviors,int *flavors) { return 46; }
static int guest_mach_message(void *message,uint32_t options,uint32_t send_size,uint32_t receive_size,uint32_t receive_name,uint32_t timeout,uint32_t notify) {
    return options&1 ? 0x10000003 : 0x10004002;  
}

struct DarwinUtsname { char system[256],node[256],release[256],version[256],machine[256]; };
static int guest_uname(struct DarwinUtsname *output) {
    if (!output) { errno=EFAULT; return -1; }
    struct utsname native;
    if (uname(&native)) return -1;
    memset(output,0,sizeof(*output));
    snprintf(output->system,sizeof(output->system),"Darwin");
    snprintf(output->node,sizeof(output->node),"%s",native.nodename);
    snprintf(output->release,sizeof(output->release),"17.0.0");
    snprintf(output->version,sizeof(output->version),"iOS 11 API compatibility on Android (%s)",native.release);
    snprintf(output->machine,sizeof(output->machine),"arm64");
    return 0;
}

static int guest_sysctl(const int *mib,unsigned count,void *old,size_t *size,const void *value,size_t value_size) {
    if (!mib || count!=2) { errno=ENOENT; return -1; }
    const char *name=NULL;
    if (mib[0]==6) {
        switch (mib[1]) {
            case 1: name="hw.machine"; break;
            case 2: name="hw.model"; break;
            case 3: name="hw.ncpu"; break;
            case 7: name="hw.pagesize"; break;
            case 24: name="hw.memsize"; break;
            case 25: name="hw.activecpu"; break;
        }
    } else if (mib[0]==1) {
        switch (mib[1]) {
            case 1: name="kern.ostype"; break;
            case 2: name="kern.osrelease"; break;
            case 4: name="kern.version"; break;
            case 65: name="kern.osversion"; break;
        }
    }
    if (!name) { errno=ENOENT; return -1; }
    return system_control(name,old,size,value,value_size);
}

static Word resolve_kernel_import(const char *name) {
    if (!strcmp(name,"_NDR_record")) return (Word)guest_ndr_record;
    const struct { const char *name; Word pointer; } entries[]={
        {"_mach_task_self_",(Word)&self_task_port},{"_mach_host_self",(Word)guest_host_self},{"_mach_thread_self",(Word)guest_mach_thread_self},
        {"_host_info",(Word)guest_host_info},{"_host_statistics",(Word)guest_host_statistics},{"_task_info",(Word)guest_task_info},
        {"_vm_read_overwrite",(Word)guest_vm_read},{"_task_threads",(Word)guest_task_threads},{"_thread_info",(Word)guest_thread_info},
        {"_thread_get_state",(Word)guest_thread_state},{"_thread_suspend",(Word)guest_thread_control},{"_thread_resume",(Word)guest_thread_control},
        {"_mach_port_allocate",(Word)guest_port_allocate},{"_mach_port_deallocate",(Word)guest_port_deallocate},{"_mach_port_insert_right",(Word)guest_port_insert},
        {"_task_set_exception_ports",(Word)guest_exception_set},{"_task_swap_exception_ports",(Word)guest_exception_swap},{"_mach_msg",(Word)guest_mach_message},
        {"_sysctl",(Word)guest_sysctl},{"_uname",(Word)guest_uname}
    };
    for (unsigned i=0;i<COUNT(entries);++i) if (!strcmp(name,entries[i].name)) return entries[i].pointer;
    return 0;
}
