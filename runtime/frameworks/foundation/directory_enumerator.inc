 
struct DirectoryFrame { DIR *directory; char *relative; };
struct DirectoryEnumerator {
    Word isa; char *root,*last;
    struct DirectoryFrame *frames; size_t depth,capacity,level;
    unsigned pending,done; Word mutation;
    struct MetalLifetime lifetime;
};
_Static_assert(sizeof(struct DirectoryFrame)==16 &&
    offsetof(struct DirectoryEnumerator,lifetime)==72 &&
    sizeof(struct DirectoryEnumerator)==88,"Directory enumerator ARM64 layout");
static void directory_enumerator_destroy(Word object) {
    struct DirectoryEnumerator *e=(void *)object;
    for (size_t i=0;i<e->depth;++i) { closedir(e->frames[i].directory); free(e->frames[i].relative); }
    free(e->frames); free(e->root); free(e->last); free(e);
}
static int directory_push(struct DirectoryEnumerator *e,const char *relative) {
    char *path=NULL;
    if (asprintf(&path,"%s%s%s",e->root,*relative ? "/" : "",relative)<0) stop("Allocation failed","NSDirectoryEnumerator");
    DIR *directory=opendir(path); free(path); if (!directory) return 0;
    if (e->depth==e->capacity) {
        size_t capacity=e->capacity ? e->capacity*2 : 8;
        if (capacity<e->capacity || capacity>SIZE_MAX/sizeof(*e->frames)) { closedir(directory); stop("Directory nesting too large","NSDirectoryEnumerator"); }
        void *frames=realloc(e->frames,capacity*sizeof(*e->frames));
        if (!frames) { closedir(directory); stop("Allocation failed","NSDirectoryEnumerator"); }
        e->frames=frames; e->capacity=capacity;
    }
    char *copy=strdup(relative); if (!copy) { closedir(directory); stop("Allocation failed","NSDirectoryEnumerator"); }
    e->frames[e->depth++]=(struct DirectoryFrame){directory,copy}; return 1;
}
static Word directory_enumerator_create(Word manager,const char *selector,Word path) {
    if (!path) return 0;
    char *root=native_path(string_bytes(path,selector)); if (!root) return 0;
    struct DirectoryEnumerator *e=calloc(1,sizeof(*e)); if (!e) { free(root); stop("Allocation failed",selector); }
    e->isa=external_class("_OBJC_CLASS_$_NSDirectoryEnumerator"); e->root=root;
    if (!directory_push(e,"")) { directory_enumerator_destroy((Word)e); return 0; }
    /* One dedicated synthetic class, O(1) existing ARC dispatch. No registry,
     * general Foundation GC, new thread, or per-frame cleanup scan. */
    metal_lifetime_init((Word)e,offsetof(struct DirectoryEnumerator,lifetime),directory_enumerator_destroy);
    return metal_object_autorelease((Word)e);
}
static Word directory_enumerator_next(struct DirectoryEnumerator *e,const char *selector) {
    if (e->done) return 0;
    if (e->pending) { e->pending=0; directory_push(e,e->last); }
    while (e->depth) {
        struct DirectoryFrame *frame=e->frames+e->depth-1;
        struct dirent *entry=readdir(frame->directory);
        if (!entry) { closedir(frame->directory); free(frame->relative); --e->depth; continue; }
        if (!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..")) continue;
        char *relative=NULL;
        if (asprintf(&relative,"%s%s%s",frame->relative,*frame->relative ? "/" : "",entry->d_name)<0) stop("Allocation failed",selector);
        int is_directory=entry->d_type==DT_DIR;
        if (entry->d_type==DT_UNKNOWN) {
            struct stat info;
            is_directory=!fstatat(dirfd(frame->directory),entry->d_name,&info,AT_SYMLINK_NOFOLLOW) && S_ISDIR(info.st_mode);
        }
        free(e->last); e->last=relative; e->pending=is_directory; e->level=e->depth;
        return copy_string(relative);
    }
    e->done=1; free(e->frames); e->frames=NULL; e->capacity=0; return 0;
}
static Word directory_enumerator_all(struct DirectoryEnumerator *e,const char *selector) {
    struct Sequence *array=(void *)sequence_empty(external_class("_OBJC_CLASS_$_NSArray"));
    for (Word value;(value=directory_enumerator_next(e,selector));) sequence_add(array,selector,value);
    return (Word)array;
}
static void directory_enumerator_skip(struct DirectoryEnumerator *e) { e->pending=0; }
static Word directory_enumerator_level(struct DirectoryEnumerator *e) { return e->level; }
static Word directory_enumerator_enumerate(struct DirectoryEnumerator *e,const char *selector,struct Enumeration *state,Word *buffer,size_t capacity) {
    size_t count=0;
    if (capacity && !buffer) stop("Nil enumeration buffer",selector);
    while (count<capacity) { Word value=directory_enumerator_next(e,selector); if (!value) break; buffer[count++]=value; }
    state->items=buffer; state->mutations=&e->mutation; state->cursor+=count; return count;
}
static Word directory_enumerator_attributes(struct DirectoryEnumerator *e,const char *selector) {
    const char *relative=!strcmp(selector,"fileAttributes") ? e->last : e->depth ? e->frames[e->depth-1].relative : "";
    if (!relative) return 0;
    char *path=NULL; if (asprintf(&path,"%s%s%s",e->root,*relative ? "/" : "",relative)<0) stop("Allocation failed",selector);
    Word result=manager_file_attributes((Word)&file_manager,selector,copy_string(path),NULL); free(path); return result;
}
static Method directory_enumerator_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver || class_receiver) return NULL;
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSFileManager") && !strcmp(selector,"enumeratorAtPath:")) return (Method)directory_enumerator_create;
    if (strcmp(receiver,"_OBJC_CLASS_$_NSDirectoryEnumerator")) return NULL;
    if (!strcmp(selector,"nextObject")) return (Method)directory_enumerator_next;
    if (!strcmp(selector,"allObjects")) return (Method)directory_enumerator_all;
    if (!strcmp(selector,"skipDescendants") || !strcmp(selector,"skipDescendents")) return (Method)directory_enumerator_skip;
    if (!strcmp(selector,"level")) return (Method)directory_enumerator_level;
    if (!strcmp(selector,"fileAttributes") || !strcmp(selector,"directoryAttributes")) return (Method)directory_enumerator_attributes;
    if (!strcmp(selector,"countByEnumeratingWithState:objects:count:")) return (Method)directory_enumerator_enumerate;
    return NULL;
}
