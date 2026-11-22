/* This IPA imports 27 fixed-argument SQLite entry points. SQLite's public C ABI
 * is shared by Apple/Android ARM64; SQL, callbacks, transactions and errors run
 * in the unmodified SQLite implementation. Only filenames cross the VFS seam. */
static void *sqlite_library;
static pthread_once_t sqlite_once=PTHREAD_ONCE_INIT;
static void load_sqlite(void) {
    sqlite_library=dlopen("libi2a_sqlite.so",RTLD_NOW|RTLD_LOCAL);
    if (sqlite_library) {
        const char *(*version)(void)=dlsym(sqlite_library,"sqlite3_libversion");
        LOG("SQLite native backend %s",version ? version() : "unknown");
    }
}
static Word sqlite_export(const char *name) {
    pthread_once(&sqlite_once,load_sqlite);
    if (!sqlite_library) stop("SQLite backend unavailable",name);
    Word entry=(Word)dlsym(sqlite_library,name);
    if (!entry) stop("SQLite entry unavailable",name);
    return entry;
}
static int sqlite_open_file(const char *name,void **database,int flags,const char *vfs) {
    int (*open_database)(const char *,void **,int,const char *)=(void *)sqlite_export("sqlite3_open_v2");
    if (!name || !database) return 21;  
    *database=NULL;
    if (!*name || !strcmp(name,":memory:")) return open_database(name,database,flags,vfs);
    if (!strncmp(name,"file:",5)) stop("SQLite URI mapping is not implemented",name);
    char *path=native_path(name);
    if (!path) return 14;  
    if (!(flags&1) && !writable_sandbox_path(path)) { free(path); return 14; }
    int result=open_database(path,database,flags,vfs);
    LOG("SQLite open %s result=%d",path,result);
    free(path);
    return result;
}
static int sqlite_open_basic(const char *name,void **database) { return sqlite_open_file(name,database,2|4,NULL); }
static Word resolve_sqlite_import(const char *name) {
    if (strncmp(name,"_sqlite3_",9)) return 0;
    if (!strcmp(name,"_sqlite3_open")) return (Word)sqlite_open_basic;
    if (!strcmp(name,"_sqlite3_open_v2")) return (Word)sqlite_open_file;
    return sqlite_export(name+1);
}
