 
static long darwin_sysconf(int name) {
    int native;
    switch (name) {
    case 1: native=_SC_ARG_MAX; break;
    case 2: native=_SC_CHILD_MAX; break;
    case 3: native=_SC_CLK_TCK; break;
    case 4: native=_SC_NGROUPS_MAX; break;
    case 5: native=_SC_OPEN_MAX; break;
    case 6: native=_SC_JOB_CONTROL; break;
    case 7: native=_SC_SAVED_IDS; break;
    case 8: native=_SC_VERSION; break;
    case 26: native=_SC_STREAM_MAX; break;
    case 27: native=_SC_TZNAME_MAX; break;
    case 29: native=_SC_PAGESIZE; break;
    case 30: native=_SC_MEMLOCK; break;
    case 31: native=_SC_MEMLOCK_RANGE; break;
    case 32: native=_SC_MEMORY_PROTECTION; break;
    case 35: native=_SC_PRIORITY_SCHEDULING; break;
    case 37: native=_SC_SEMAPHORES; break;
    case 38: native=_SC_FSYNC; break;
    case 39: native=_SC_SHARED_MEMORY_OBJECTS; break;
    case 40: native=_SC_SYNCHRONIZED_IO; break;
    case 41: native=_SC_TIMERS; break;
    case 47: native=_SC_MAPPED_FILES; break;
    case 50: native=_SC_SEM_VALUE_MAX; break;
    case 56: native=_SC_IOV_MAX; break;
    case 57: native=_SC_NPROCESSORS_CONF; break;
    case 58: native=_SC_NPROCESSORS_ONLN; break;
    case 70: native=_SC_GETGR_R_SIZE_MAX; break;
    case 71: native=_SC_GETPW_R_SIZE_MAX; break;
    case 72: native=_SC_HOST_NAME_MAX; break;
    case 73: native=_SC_LOGIN_NAME_MAX; break;
    case 74: native=_SC_MONOTONIC_CLOCK; break;
    case 82: native=_SC_THREAD_ATTR_STACKADDR; break;
    case 83: native=_SC_THREAD_ATTR_STACKSIZE; break;
    case 85: native=_SC_THREAD_DESTRUCTOR_ITERATIONS; break;
    case 86: native=_SC_THREAD_KEYS_MAX; break;
    case 89: native=_SC_THREAD_PRIORITY_SCHEDULING; break;
    case 90: native=_SC_THREAD_PROCESS_SHARED; break;
    case 91: native=_SC_THREAD_SAFE_FUNCTIONS; break;
    case 93: native=_SC_THREAD_STACK_MIN; break;
    case 94: native=_SC_THREAD_THREADS_MAX; break;
    case 96: native=_SC_THREADS; break;
    case 101: native=_SC_TTY_NAME_MAX; break;
    case 107: native=_SC_ATEXIT_MAX; break;
    case 200: native=_SC_PHYS_PAGES; break;
    default: errno=EINVAL; return -1;
    }
    return sysconf(native);
}
struct DarwinRuneRange { int count; void *ranges; };
struct DarwinRuneLocale {
    char magic[8],encoding[32];
    void *get_rune,*put_rune;
    int invalid;
    uint32_t type[256];
    int lower[256],upper[256];
    struct DarwinRuneRange type_extended,lower_extended,upper_extended;
    void *variable; int variable_size,class_count; void *classes;
};
static struct DarwinRuneLocale darwin_default_runes;
static pthread_once_t darwin_runes_once=PTHREAD_ONCE_INIT;
static void darwin_runes_initialize(void) {
    _Static_assert(offsetof(struct DarwinRuneLocale,type)==60,"Darwin cached rune offset");
    _Static_assert(offsetof(struct DarwinRuneLocale,lower)==1084,"Darwin lowercase map offset");
    memcpy(darwin_default_runes.magic,"RuneMagA",8);
    memcpy(darwin_default_runes.encoding,"NONE",5);
    darwin_default_runes.invalid=0xfffd;
    for (unsigned ch=0;ch<256;++ch) {
        unsigned mask=0;
        if (ch<32 || ch==127) mask|=0x200;
        if (ch==' ' || (ch>=9 && ch<=13)) mask|=0x4000;
        if (ch==' ' || ch==9) mask|=0x20000;
        if (ch>=32 && ch<127) mask|=0x40000;
        if (ch>32 && ch<127) mask|=0x800;
        if (ch>='0' && ch<='9') mask|=0x10400|(ch-'0');
        if (ch>='A' && ch<='Z') mask|=0x8100;
        if (ch>='a' && ch<='z') mask|=0x1100;
        if (ch>='A' && ch<='F') mask|=0x10000|(ch-'A'+10);
        if (ch>='a' && ch<='f') mask|=0x10000|(ch-'a'+10);
        if ((mask&0x800) && !(mask&0x500)) mask|=0x2000;
        darwin_default_runes.type[ch]=mask;
        darwin_default_runes.lower[ch]=ch>='A' && ch<='Z' ? ch+32 : ch;
        darwin_default_runes.upper[ch]=ch>='a' && ch<='z' ? ch-32 : ch;
    }
}
 
static int darwin_maskrune(int rune,unsigned long mask) {
    pthread_once(&darwin_runes_once,darwin_runes_initialize);
    return (unsigned)rune<256 ? (int)(darwin_default_runes.type[rune]&mask) : 0;
}
static int darwin_toupper(int rune) { return rune>='a' && rune<='z' ? rune-32 : rune; }
static int darwin_tolower(int rune) { return rune>='A' && rune<='Z' ? rune+32 : rune; }
struct DarwinSinCosFloat { float sine,cosine; };
struct DarwinSinCosDouble { double sine,cosine; };
static struct DarwinSinCosFloat darwin_sincosf(float value) {
    struct DarwinSinCosFloat result; sincosf(value,&result.sine,&result.cosine); return result;
}
static struct DarwinSinCosDouble darwin_sincos(double value) {
    struct DarwinSinCosDouble result; sincos(value,&result.sine,&result.cosine); return result;
}
static int darwin_isnanf(float value) { return isnan(value)!=0; }
static int darwin_isfinitef(float value) { return isfinite(value)!=0; }
static int darwin_isinff(float value) { return isinf(value)!=0; }
static double darwin_exp10(double value) { return pow(10.0,value); }
static __attribute__((noreturn)) void darwin_assert(const char *function,const char *file,int line,const char *expression) {
    char detail[420];
    snprintf(detail,sizeof(detail),"%s at %s:%d: %s",function ? function : "?",file ? file : "?",line,expression ? expression : "?");
    void *frames[32];
    int count=darwin_backtrace(frames,COUNT(frames));
    for (int i=0;i<count;++i) if (in_image((Word)frames[i]))
        LOG("Assertion guest frame %d: 0x%lx",i,(Word)frames[i]-aslr+GUEST_IMAGE_BASE);
    stop("Original assertion failed",detail);
}
static void darwin_dcache_flush(void *address,size_t length) {
    if (!length) return;
    Word start=(Word)address;
    if (length>UINTPTR_MAX-start) stop("Invalid cache range","sys_dcache_flush");
    Word ctr; __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    Word line=4UL<<((ctr>>16)&15),last=(start+length-1)&~(line-1);
    for (Word cursor=start&~(line-1);;) {
        __asm__ volatile("dc cvac, %0" :: "r"(cursor) : "memory");
        if (cursor==last) break;
        cursor+=line;
    }
    __asm__ volatile("dsb ish" ::: "memory");
}
static const float darwin_identity_float3x3[12] __attribute__((aligned(16)))={1,0,0,0, 0,1,0,0, 0,0,1,0};
struct DarwinLocaleConv {
    char *decimal_point,*thousands_sep,*grouping,*int_curr_symbol,*currency_symbol;
    char *mon_decimal_point,*mon_thousands_sep,*mon_grouping,*positive_sign,*negative_sign;
    char int_frac_digits,frac_digits,p_cs_precedes,p_sep_by_space,n_cs_precedes,n_sep_by_space,p_sign_posn,n_sign_posn;
    char int_p_cs_precedes,int_n_cs_precedes,int_p_sep_by_space,int_n_sep_by_space,int_p_sign_posn,int_n_sign_posn;
};
static struct DarwinLocaleConv *darwin_localeconv(void) {
    static struct DarwinLocaleConv value={".","","","","","","","","","",127,127,127,127,127,127,127,127,127,127,127,127,127,127};
    return &value;
}
static char *darwin_setlocale(int category,const char *name) {
    static char c_name[]="C";
    static const char *const environment[]={"LC_ALL","LC_COLLATE","LC_CTYPE","LC_MONETARY","LC_NUMERIC","LC_TIME","LC_MESSAGES"};
    if ((unsigned)category>=COUNT(environment)) { errno=EINVAL; return NULL; }
    if (!name) return c_name;
    if (!*name) {
        name=getenv("LC_ALL");
        if (!name || !*name) name=getenv(environment[category]);
        if (!name || !*name) name=getenv("LANG");
        if (!name || !*name) name="C";
    }
    if (strcmp(name,"C") && strcmp(name,"POSIX")) { errno=ENOENT; return NULL; }
    return c_name;
}
static struct GuestRange darwin_intersection_range(struct GuestRange a,struct GuestRange b) {
    size_t start=a.location>b.location ? a.location : b.location;
    size_t a_end=a.length>SIZE_MAX-a.location ? SIZE_MAX : a.location+a.length;
    size_t b_end=b.length>SIZE_MAX-b.location ? SIZE_MAX : b.location+b.length;
    size_t end=a_end<b_end ? a_end : b_end;
    return end>start ? (struct GuestRange){start,end-start} : (struct GuestRange){0,0};
}
static Word resolve_libsystem_import(const char *name) {
    if (!strcmp(name,"__setjmp")) return (Word)darwin_setjmp;
    if (!strcmp(name,"__longjmp")) return (Word)darwin_longjmp;
    if (!strcmp(name,"_localeconv")) return (Word)darwin_localeconv;
    if (!strcmp(name,"_setlocale")) return (Word)darwin_setlocale;
    if (!strcmp(name,"_NSIntersectionRange")) return (Word)darwin_intersection_range;
    if (!strcmp(name,"_NSLogv")) return (Word)foundation_log;
    if (!strcmp(name,"_sysconf")) return (Word)darwin_sysconf;
    if (!strcmp(name,"__DefaultRuneLocale")) {
        pthread_once(&darwin_runes_once,darwin_runes_initialize); return (Word)&darwin_default_runes;
    }
    if (!strcmp(name,"___toupper")) return (Word)darwin_toupper;
    if (!strcmp(name,"___tolower")) return (Word)darwin_tolower;
    if (!strcmp(name,"___maskrune")) return (Word)darwin_maskrune;
    if (!strcmp(name,"___isnanf")) return (Word)darwin_isnanf;
    if (!strcmp(name,"___isfinitef")) return (Word)darwin_isfinitef;
    if (!strcmp(name,"___isinff")) return (Word)darwin_isinff;
    if (!strcmp(name,"___sincosf_stret")) return (Word)darwin_sincosf;
    if (!strcmp(name,"___sincos_stret")) return (Word)darwin_sincos;
    if (!strcmp(name,"___exp10")) return (Word)darwin_exp10;
    if (!strcmp(name,"___assert_rtn")) return (Word)darwin_assert;
    if (!strcmp(name,"_sys_dcache_flush")) return (Word)darwin_dcache_flush;
    if (!strcmp(name,"_matrix_identity_float3x3")) return (Word)darwin_identity_float3x3;
    return 0;
}
