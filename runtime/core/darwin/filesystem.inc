 

static const char *directory_suffix(Word directory) {
    switch (directory) {
        case 5: return "Library";
        case 9: return "Documents";
        case 13: return "Caches";
        case 14: return "";
        case 21: return "Library/Application Support";
        default: return NULL;
    }
}
static Word manager_create_path(Word,const char *,Word,Word,Word,Word *);
static Word directory_path(Word directory) {
    const char *suffix=directory_suffix(directory); if (!suffix) stop("Unsupported sandbox directory","NSSearchPathForDirectoriesInDomains");
    if (!*suffix) return copy_string(files_directory);
    Word path=append_path(copy_string(files_directory),"sandbox directory",copy_string(suffix));
    const char *bytes=string_bytes(path,"sandbox directory");
    if (strchr(suffix,'/')) { if (!manager_create_path((Word)&file_manager,"Sandbox directory",path,1,0,NULL)) stop("Sandbox directory creation failed",bytes); }
    else if (mkdir(bytes,0700) && errno!=EEXIST) stop("Sandbox directory creation failed",bytes);
    return path;
}

static Word search_directories(Word directory,Word domains,unsigned char expand) {
    if (domains!=1) stop("Unsupported search domain","NSSearchPathForDirectoriesInDomains");
    Word path=directory_path(directory);
    return new_array(external_class("_OBJC_CLASS_$_NSArray"),"search path",&path,1);
}

static Word manager_directory_urls(Word self,const char *selector,Word directory,Word domains) {
    if (domains!=1) stop("Unsupported search domain",selector);
    Word url=file_url(directory_path(directory));
    return new_array(external_class("_OBJC_CLASS_$_NSArray"),selector,&url,1);
}

static Word home_directory(void) { return copy_string(files_directory); }
static Word temporary_directory(void) {
    Word root=directory_path(13);
    Word path=append_path(root,"temporary directory",copy_string("tmp"));
    const char *bytes=string_bytes(path,"temporary directory");
    if (mkdir(bytes,0700) && errno!=EEXIST) stop("Temporary directory creation failed",bytes);
    return append_text(path,"temporary directory",copy_string("/"));
}

static Word error_with_domain(Word cls,const char *selector,Word domain,long code,Word info) {
    struct HostObject *error=calloc(1,sizeof(*error));
    if (!error) stop("Allocation failed",selector);
    error->isa=cls; error->content[0]=domain; error->content[1]=(Word)code;
    error->content[2]=info ? map_copy((void *)info,"copy") : empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));
    return (Word)error;
}
static Word error_domain(struct HostObject *error) { return error->content[0]; }
static Word error_code(struct HostObject *error) { return error->content[1]; }
static Word error_info(struct HostObject *error) { return error->content[2]; }
static Word error_description(struct HostObject *error) {
    struct GuestMap *info=(void *)error->content[2];
    Word value=info ? map_get(info,"localizedDescription",copy_string("NSLocalizedDescription")) : 0;
    return value ? value : copy_string(strerror((int)error->content[1]));
}

static Word file_failure(Word *error,const char *path,int code) {
    if (error) {
        struct GuestMap *info=(void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));
        map_set(info,"file error",copy_string(path),copy_string("NSFilePath"));
        map_set(info,"file error",copy_string(strerror(code)),copy_string("NSLocalizedDescription"));
        *error=error_with_domain(external_class("_OBJC_CLASS_$_NSError"),"file error",copy_string("NSPOSIXErrorDomain"),code,(Word)info);
    }
    return 0;
}

static int writable_sandbox_path(const char *path) {
    size_t root=strlen(files_directory);
    if (strncmp(path,files_directory,root) || path[root]!='/' || strstr(path,"/../") || !strcmp(strrchr(path,'/'),"/..")) {
        errno=EACCES;
        return 0;
    }
    return 1;
}

static Word manager_exists(Word self,const char *selector,Word filename,unsigned char *is_directory) {
    if (!filename) { if (is_directory) *is_directory=0; return 0; }
    const char *logical=string_bytes(filename,selector);
    const char *relative=bundle_relative(logical);
    if (relative) {
        JNIEnv *env=attached_environment(selector);
        jclass cls=(*env)->GetObjectClass(env,activity);
        jmethodID query=(*env)->GetMethodID(env,cls,"bundleEntryType","(Ljava/lang/String;)I");
        jstring key=(*env)->NewStringUTF(env,relative);
        jint type=(*env)->CallIntMethod(env,activity,query,key);
        (*env)->DeleteLocalRef(env,key); (*env)->DeleteLocalRef(env,cls);
        if (is_directory) *is_directory=type==2;
        return type!=0;
    }
    char *path=native_path(string_bytes(filename,selector));
    if (!path) { if (is_directory) *is_directory=0; return 0; }
    struct stat info;
    int exists=stat(path,&info)==0;
    if (is_directory) *is_directory=exists && S_ISDIR(info.st_mode);
    free(path);
    return exists;
}

static Word manager_exists_simple(Word self,const char *selector,Word path) { return manager_exists(self,selector,path,NULL); }

static Word manager_file_attributes(Word self,const char *selector,Word filename,Word *error) {
    const char *name=string_bytes(filename,selector);
    char *path=native_path(name);
    if (!path) return file_failure(error,name,ENOENT);
    struct stat info;
    if (stat(path,&info)) { int code=errno; free(path); return file_failure(error,name,code); }
    free(path);
    struct GuestMap *attributes=(void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));
    map_set(attributes,selector,number_integer(info.st_size),copy_string("NSFileSize"));
    map_set(attributes,selector,number_integer(info.st_mode&07777),copy_string("NSFilePosixPermissions"));
    Word modified=date_epoch(external_class("_OBJC_CLASS_$_NSDate"),selector,info.st_mtim.tv_sec+info.st_mtim.tv_nsec*1e-9);
    map_set(attributes,selector,modified,copy_string("NSFileModificationDate"));
     
    map_set(attributes,selector,copy_string(S_ISDIR(info.st_mode) ? "NSFileTypeDirectory" : "NSFileTypeRegular"),copy_string("NSFileType"));
    return (Word)attributes;
}

static Word manager_filesystem_attributes(Word self,const char *selector,Word filename,Word *error) {
    const char *path=string_bytes(filename,selector);
    struct statvfs info;
    if (statvfs(path,&info)) return file_failure(error,path,errno);
    struct GuestMap *attributes=(void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));
    map_set(attributes,selector,number_integer((Word)info.f_blocks*info.f_frsize),copy_string("NSFileSystemSize"));
    map_set(attributes,selector,number_integer((Word)info.f_bavail*info.f_frsize),copy_string("NSFileSystemFreeSize"));
    return (Word)attributes;
}

static Word manager_list(Word self,const char *selector,Word filename,Word *error) {
    const char *path=string_bytes(filename,selector);
    const char *relative=bundle_relative(path);
    if (relative) {
        JNIEnv *env=attached_environment(selector);
        jclass cls=(*env)->GetObjectClass(env,activity);
        jmethodID list=(*env)->GetMethodID(env,cls,"bundleList","(Ljava/lang/String;)[Ljava/lang/String;");
        jstring key=(*env)->NewStringUTF(env,relative);
        jobjectArray names=(*env)->CallObjectMethod(env,activity,list,key);
        (*env)->DeleteLocalRef(env,key); (*env)->DeleteLocalRef(env,cls);
        if (!names) return file_failure(error,path,ENOENT);
        struct Sequence *items=(void *)sequence_empty(external_class("_OBJC_CLASS_$_NSArray"));
        jsize count=(*env)->GetArrayLength(env,names);
        for (jsize index=0;index<count;++index) {
            jstring name=(*env)->GetObjectArrayElement(env,names,index);
            sequence_add(items,selector,string_from_java(env,name,selector));
            (*env)->DeleteLocalRef(env,name);
        }
        (*env)->DeleteLocalRef(env,names);
        return (Word)items;
    }
    DIR *directory=opendir(path);
    if (!directory) return file_failure(error,path,errno);
    struct Sequence *items=(void *)sequence_empty(external_class("_OBJC_CLASS_$_NSArray"));
    struct dirent *entry;
    while ((entry=readdir(directory))) {
        if (!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..")) continue;
        sequence_add(items,selector,copy_string(entry->d_name));
    }
    closedir(directory);
    return (Word)items;
}

static Word manager_set_attributes(Word self,const char *selector,struct GuestMap *attributes,Word filename,Word *error) {
    const char *path=string_bytes(filename,selector);
    if (!writable_sandbox_path(path)) return file_failure(error,path,errno);
    for (size_t index=0;index<attributes->count;++index) {
        const struct MapEntry *entry=attributes->entries+index;
        const char *key=string_bytes(entry->key,selector);
        if (!strcmp(key,"NSFilePosixPermissions")) {
            mode_t mode=(mode_t)numeric_integer((void *)entry->value)&07777;
            if (chmod(path,mode)) return file_failure(error,path,errno);
        } else if (!strcmp(key,"NSFileImmutable")) {
            if (numeric_bool((void *)entry->value)) return file_failure(error,path,ENOTSUP);
        } else if (!strcmp(key,"NSFileProtectionKey")) {
            if (strcmp(string_bytes(entry->value,selector),"NSFileProtectionNone")) return file_failure(error,path,ENOTSUP);
        } else return file_failure(error,path,ENOTSUP);
    }
    return 1;
}

static Word manager_create_path(Word self,const char *selector,Word path,Word intermediates,Word attributes,Word *error) {
    const char *name=string_bytes(path,selector);
    if (!writable_sandbox_path(name)) return file_failure(error,name,errno);
    if (!make_directory(self,selector,(void *)file_url(path),intermediates,0,NULL)) return file_failure(error,name,errno);
    return attributes ? manager_set_attributes(self,selector,(void *)attributes,path,error) : 1;
}

static int remove_tree(const char *path) {
    struct stat info;
    if (lstat(path,&info)) return -1;
    if (!S_ISDIR(info.st_mode)) return unlink(path);
    DIR *directory=opendir(path);
    if (!directory) return -1;
    struct dirent *entry;
    int result=0;
    while (!result && (entry=readdir(directory))) {
        if (!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..")) continue;
        char *child=NULL;
        if (asprintf(&child,"%s/%s",path,entry->d_name)<0) { errno=ENOMEM; result=-1; break; }
        result=remove_tree(child);
        free(child);
    }
    int code=errno;
    closedir(directory);
    if (!result) result=rmdir(path);
    else errno=code;
    return result;
}

static Word manager_remove(Word self,const char *selector,Word filename,Word *error) {
    const char *path=string_bytes(filename,selector);
    if (!writable_sandbox_path(path) || remove_tree(path)) return file_failure(error,path,errno);
    return 1;
}

static Word manager_move(Word self,const char *selector,Word source,Word destination,Word *error) {
    const char *from=string_bytes(source,selector), *to=string_bytes(destination,selector);
    if (!writable_sandbox_path(from) || !writable_sandbox_path(to) || rename(from,to)) return file_failure(error,from,errno);
    return 1;
}

static Word manager_writable(Word self,const char *selector,Word filename) {
    const char *path=string_bytes(filename,selector);
    return writable_sandbox_path(path) && access(path,W_OK)==0;
}
static Word manager_readable(Word self,const char *selector,Word filename) {
    const char *name=string_bytes(filename,selector);
    if (bundle_relative(name)!=NULL) return manager_exists_simple(self,selector,filename);
    return access(name,R_OK)==0;
}
static Word manager_contents(Word self,const char *selector,Word filename) { return data_from_file(external_class("_OBJC_CLASS_$_NSData"),selector,filename); }
static Word manager_create_file(Word self,const char *selector,Word filename,struct HostObject *data,struct GuestMap *attributes) {
    const char *path=string_bytes(filename,selector);
    if (!writable_sandbox_path(path)) return 0;
    int descriptor=open(path,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0666);
    if (descriptor<0) return 0;
    size_t length=data ? data->content[0] : 0,position=0;
    const unsigned char *bytes=data ? (void *)data_buffer(data) : NULL;
    int success=1;
    while (position<length) {
        ssize_t count=write(descriptor,bytes+position,length-position);
        if (count<0 && errno==EINTR) continue;
        if (count<=0) { success=0; break; }
        position+=(size_t)count;
    }
    if (close(descriptor)) success=0;
    return success && (!attributes || manager_set_attributes(self,selector,attributes,filename,NULL));
}
static Word error_init(Word self,const char *selector,Word domain,long code,Word info) {
    Word result=error_with_domain(read_word(self),selector,domain,code,info);
    free((void *)self);
    return result;
}

static Word manager_single_directory(Word object,const char *selector,Word directory,Word domain,Word appropriate,unsigned char create,Word *error) {
    if (error) *error=0;
    const char *suffix=directory_suffix(directory);
    if (domain!=1 || !suffix) return file_failure(error,"Sandbox directory",ENOTSUP);
    Word path=*suffix ? append_path(copy_string(files_directory),selector,copy_string(suffix)) : copy_string(files_directory);
    if (create && !manager_create_path(object,selector,path,1,0,error)) return 0;
    struct stat metadata; const char *bytes=string_bytes(path,selector);
    if (stat(bytes,&metadata)) return file_failure(error,bytes,errno);
    if (!S_ISDIR(metadata.st_mode)) return file_failure(error,bytes,ENOTDIR);
    return file_url(path);
}
static Method filesystem_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver) return NULL;
    if (!class_receiver && !strcmp(receiver,"_OBJC_CLASS_$_NSFileManager")) {
        if (!strcmp(selector,"URLForDirectory:inDomain:appropriateForURL:create:error:")) return (Method)manager_single_directory;
        if (!strcmp(selector,"URLsForDirectory:inDomains:")) return (Method)manager_directory_urls;
        if (!strcmp(selector,"fileExistsAtPath:")) return (Method)manager_exists_simple;
        if (!strcmp(selector,"fileExistsAtPath:isDirectory:")) return (Method)manager_exists;
        if (!strcmp(selector,"attributesOfItemAtPath:error:")) return (Method)manager_file_attributes;
        if (!strcmp(selector,"attributesOfFileSystemForPath:error:")) return (Method)manager_filesystem_attributes;
        if (!strcmp(selector,"contentsOfDirectoryAtPath:error:")) return (Method)manager_list;
        if (!strcmp(selector,"createDirectoryAtPath:withIntermediateDirectories:attributes:error:")) return (Method)manager_create_path;
        if (!strcmp(selector,"removeItemAtPath:error:")) return (Method)manager_remove;
        if (!strcmp(selector,"moveItemAtPath:toPath:error:")) return (Method)manager_move;
        if (!strcmp(selector,"isWritableFileAtPath:")) return (Method)manager_writable;
        if (!strcmp(selector,"isReadableFileAtPath:")) return (Method)manager_readable;
        if (!strcmp(selector,"contentsAtPath:")) return (Method)manager_contents;
        if (!strcmp(selector,"createFileAtPath:contents:attributes:")) return (Method)manager_create_file;
        if (!strcmp(selector,"setAttributes:ofItemAtPath:error:")) return (Method)manager_set_attributes;
    }
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSError")) {
        if (class_receiver && !strcmp(selector,"errorWithDomain:code:userInfo:")) return (Method)error_with_domain;
        if (!class_receiver && !strcmp(selector,"initWithDomain:code:userInfo:")) return (Method)error_init;
        if (!class_receiver && !strcmp(selector,"domain")) return (Method)error_domain;
        if (!class_receiver && !strcmp(selector,"code")) return (Method)error_code;
        if (!class_receiver && !strcmp(selector,"userInfo")) return (Method)error_info;
        if (!class_receiver && !strcmp(selector,"localizedDescription")) return (Method)error_description;
        if (!class_receiver && (!strcmp(selector,"copy") || !strcmp(selector,"copyWithZone:"))) return (Method)identity;
    }
    return NULL;
}
