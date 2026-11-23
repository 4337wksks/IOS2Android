static Word manager_remove_url(Word self,const char *selector,struct HostObject *url,Word *error) {
    if (!url || !url->content[1]) return file_failure(error,"Non-file URL",EINVAL);
    return manager_remove(self,selector,url_path(url),error);
}
static Word manager_list_urls(Word self,const char *selector,struct HostObject *url,Word keys,Word options,Word *error) {
    if (!url || !url->content[1]) return file_failure(error,"Non-file URL",EINVAL);
    Word path=url_path(url);
    struct Sequence *names=(void *)manager_list(self,selector,path,error);
    if (!names) return 0;
    struct Sequence *result=(void *)sequence_empty(external_class("_OBJC_CLASS_$_NSArray"));
    for (size_t index=0;index<names->count;++index) {
        const char *name=string_bytes(names->items[index],selector);
        if ((options&4) && name[0]=='.') continue;
        sequence_add(result,selector,url_append_path(url,selector,names->items[index]));
    }
    return (Word)result;
}
static Word file_url_reachable(struct HostObject *url,const char *selector,Word *error) {
    if (!url->content[1]) return file_failure(error,"Non-file URL",EINVAL);
    Word path=url_path(url);
    return manager_exists_simple((Word)&file_manager,selector,path) ? 1 : file_failure(error,string_bytes(path,selector),ENOENT);
}
static Word file_url_resource(struct HostObject *url,const char *selector,Word *output,Word key,Word *error) {
    if (output) *output=0;
    if (!output || !url->content[1]) return file_failure(error,"Non-file URL",EINVAL);
    const char *name=string_bytes(key,selector);
    Word path=url_path(url);
    if (!strcmp(name,"NSURLNameKey")) { *output=path_basename(path,selector); return 1; }
    if (!strcmp(name,"NSURLIsExcludedFromBackupKey")) { *output=number_bool(external_class("_OBJC_CLASS_$_NSNumber"),selector,1); return 1; }
    unsigned char directory=0;
    if (!manager_exists((Word)&file_manager,selector,path,&directory)) return file_failure(error,string_bytes(path,selector),ENOENT);
    if (!strcmp(name,"NSURLIsDirectoryKey") || !strcmp(name,"NSURLIsRegularFileKey")) {
        *output=number_bool(external_class("_OBJC_CLASS_$_NSNumber"),selector,!strcmp(name,"NSURLIsDirectoryKey") ? directory : !directory);
        return 1;
    }
    Word attributes=manager_file_attributes((Word)&file_manager,selector,path,error);
    if (!attributes) return 0;
    const char *field=!strcmp(name,"NSURLFileSizeKey") ? "NSFileSize" : !strcmp(name,"NSURLContentModificationDateKey") ? "NSFileModificationDate" :
        !strcmp(name,"NSURLCreationDateKey") ? "NSFileCreationDate" : NULL;
    if (!field) return file_failure(error,string_bytes(path,selector),ENOTSUP);
    *output=map_get((void *)attributes,selector,copy_string(field));
    return *output ? 1 : file_failure(error,string_bytes(path,selector),ENOTSUP);
}
static Word file_url_set_resource(struct HostObject *url,const char *selector,Word value,Word key,Word *error) {
    if (url->content[1] && !strcmp(string_bytes(key,selector),"NSURLIsExcludedFromBackupKey") && numeric_bool((void *)value)) return 1;
    return file_failure(error,string_bytes(url_path(url),selector),ENOTSUP);
}
static Word file_url_resources(struct HostObject *url,const char *selector,struct Sequence *keys,Word *error) {
    if (error) *error=0;
    struct GuestMap *result=(void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));
    for (size_t i=0;keys && i<keys->count;++i) {
        Word value=0;
        if (!file_url_resource(url,selector,&value,keys->items[i],error)) return 0;
        if (value) map_set(result,selector,value,keys->items[i]);
    }
    return (Word)result;
}
static Word file_url_set_resources(struct HostObject *url,const char *selector,struct GuestMap *values,Word *error) {
    if (error) *error=0;
    for (size_t i=0;values && i<values->count;++i)
        if (!file_url_set_resource(url,selector,values->entries[i].value,values->entries[i].key,error)) return 0;
    return 1;
}
static Word string_file_url(Word cls,const char *selector,struct HostObject *url,Word encoding,Word *error) {
    if (!url) return file_failure(error,"Nil URL",EINVAL);
    Word data=data_url_options(external_class("_OBJC_CLASS_$_NSData"),selector,url,0,error);
    if (!data) return 0;
    Word result=string_from_data(cls,selector,(void *)data,encoding);
    free((void *)data);
    if(!result && error)*error=error_with_domain(external_class("_OBJC_CLASS_$_NSError"),selector,copy_string("NSCocoaErrorDomain"),261,0);
    return result;
}
static Word string_file_detect(Word cls,const char *selector,struct HostObject *url,Word *used_encoding,Word *error) {
    if (!url) return file_failure(error,"Nil URL",EINVAL);
    struct HostObject *data=(void *)data_url_options(external_class("_OBJC_CLASS_$_NSData"),selector,url,0,error);
    if (!data) return 0;
    const unsigned char *bytes=(void *)data_buffer(data); size_t length=data->content[0],skip=0;
    Word encoding=4;
    if (length>=4 && (!memcmp(bytes,"\0\0\xfe\xff",4) || !memcmp(bytes,"\xff\xfe\0\0",4))) encoding=0x8c000100;
    else if (length>=2 && ((bytes[0]==0xff && bytes[1]==0xfe) || (bytes[0]==0xfe && bytes[1]==0xff))) encoding=10;
    else if (length>=3 && !memcmp(bytes,"\xef\xbb\xbf",3)) skip=3;
    Word result=text_decode(cls,bytes+skip,length-skip,encoding); free(data);
    if (!result) {
        if (error) *error=error_with_domain(external_class("_OBJC_CLASS_$_NSError"),selector,copy_string("NSCocoaErrorDomain"),261,0);
        return 0;
    }
    if (used_encoding) *used_encoding=encoding;
    return result;
}
static Word string_file_path(Word cls,const char *selector,Word path,Word encoding,Word *error) {
    if (!path) return file_failure(error,"Nil file path",EINVAL);
    struct HostObject *url=(void *)file_url(path);
    Word result=string_file_url(cls,selector,url,encoding,error); free(url); return result;
}
static Word string_file_path_detect(Word cls,const char *selector,Word path,Word *used_encoding,Word *error) {
    if (!path) return file_failure(error,"Nil file path",EINVAL);
    struct HostObject *url=(void *)file_url(path);
    Word result=string_file_detect(cls,selector,url,used_encoding,error); free(url); return result;
}
static Word string_init_file_path(Word object,const char *selector,Word path,Word encoding,Word *error) {
    Word result=string_file_path(read_word(object),selector,path,encoding,error); free((void *)object); return result;
}
static Word string_init_file_path_detect(Word object,const char *selector,Word path,Word *used_encoding,Word *error) {
    Word result=string_file_path_detect(read_word(object),selector,path,used_encoding,error); free((void *)object); return result;
}
static Word string_init_file_url(Word object,const char *selector,struct HostObject *url,Word encoding,Word *error) {
    Word result=string_file_url(read_word(object),selector,url,encoding,error); free((void *)object); return result;
}
static Word string_init_file_url_detect(Word object,const char *selector,struct HostObject *url,Word *used_encoding,Word *error) {
    Word result=string_file_detect(read_word(object),selector,url,used_encoding,error); free((void *)object); return result;
}
static int copy_private_file(const char *source,const char *destination,unsigned depth) {
    if (depth>128) { errno=ELOOP; return -1; }
    struct stat info;
    if (lstat(source,&info)) return -1;
    if (S_ISDIR(info.st_mode)) {
        if (mkdir(destination,info.st_mode&0777)) return -1;
        DIR *dir=opendir(source);
        if (!dir) return -1;
        struct dirent *entry;
        int error=0;
        while (!error && (entry=readdir(dir))) {
            if (!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..")) continue;
            char *from=NULL,*to=NULL;
            if (asprintf(&from,"%s/%s",source,entry->d_name)<0 || asprintf(&to,"%s/%s",destination,entry->d_name)<0) error=ENOMEM;
            else if (copy_private_file(from,to,depth+1)) error=errno;
            free(from); free(to);
        }
        closedir(dir);
        if (error) { errno=error; return -1; }
        return 0;
    }
    if (!S_ISREG(info.st_mode)) { errno=ENOTSUP; return -1; }
    int from=io_open(source,O_RDONLY|O_CLOEXEC,0);
    if (from<0) return -1;
    int to=open(destination,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,info.st_mode&0777);
    if (to<0) { int error=errno; io_close(from); errno=error; return -1; }
    char buffer[65536]; int error=0;
    for (;;) {
        ssize_t count=io_read(from,buffer,sizeof(buffer));
        if (count<0 && errno==EINTR) continue;
        if (count<0) { error=errno; break; }
        if (!count) break;
        size_t position=0;
        while (position<(size_t)count) {
            ssize_t wrote=write(to,buffer+position,(size_t)count-position);
            if (wrote<0 && errno==EINTR) continue;
            if (wrote<=0) { error=wrote<0 ? errno : EIO; break; }
            position+=(size_t)wrote;
        }
        if (error) break;
    }
    io_close(from);
    if (close(to) && !error) error=errno;
    if (error) { unlink(destination); errno=error; return -1; }
    return 0;
}
static Word manager_copy(Word self,const char *selector,Word source,Word destination,Word *error) {
    const char *to=string_bytes(destination,selector);
    if (!writable_sandbox_path(to)) return file_failure(error,to,errno);
    const char *from=string_bytes(source,selector);
    int result=copy_private_file(from,to,0),code=errno;
    return result ? file_failure(error,to,code) : 1;
}
static Word manager_copy_url(Word self,const char *selector,struct HostObject *source,struct HostObject *destination,Word *error) {
    if (!source || !destination || !source->content[1] || !destination->content[1]) return file_failure(error,"Non-file URL",EINVAL);
    return manager_copy(self,selector,url_path(source),url_path(destination),error);
}
static Method filesystem_url_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver) return NULL;
    if (!class_receiver && !strcmp(receiver,"_OBJC_CLASS_$_NSFileManager")) {
        if (!strcmp(selector,"removeItemAtURL:error:")) return (Method)manager_remove_url;
        if (!strcmp(selector,"contentsOfDirectoryAtURL:includingPropertiesForKeys:options:error:")) return (Method)manager_list_urls;
        if (!strcmp(selector,"copyItemAtPath:toPath:error:")) return (Method)manager_copy;
        if (!strcmp(selector,"copyItemAtURL:toURL:error:")) return (Method)manager_copy_url;
    }
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSURL") && !class_receiver) {
        if (!strcmp(selector,"checkResourceIsReachableAndReturnError:")) return (Method)file_url_reachable;
        if (!strcmp(selector,"getResourceValue:forKey:error:")) return (Method)file_url_resource;
        if (!strcmp(selector,"setResourceValue:forKey:error:")) return (Method)file_url_set_resource;
        if (!strcmp(selector,"resourceValuesForKeys:error:")) return (Method)file_url_resources;
        if (!strcmp(selector,"setResourceValues:error:")) return (Method)file_url_set_resources;
    }
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSString") || !strcmp(receiver,"_OBJC_CLASS_$_NSMutableString")) {
        if (class_receiver) {
            if (!strcmp(selector,"stringWithContentsOfURL:encoding:error:")) return (Method)string_file_url;
            if (!strcmp(selector,"stringWithContentsOfURL:usedEncoding:error:")) return (Method)string_file_detect;
            if (!strcmp(selector,"stringWithContentsOfFile:encoding:error:")) return (Method)string_file_path;
            if (!strcmp(selector,"stringWithContentsOfFile:usedEncoding:error:")) return (Method)string_file_path_detect;
        } else {
            if (!strcmp(selector,"initWithContentsOfFile:encoding:error:")) return (Method)string_init_file_path;
            if (!strcmp(selector,"initWithContentsOfFile:usedEncoding:error:")) return (Method)string_init_file_path_detect;
            if (!strcmp(selector,"initWithContentsOfURL:encoding:error:")) return (Method)string_init_file_url;
            if (!strcmp(selector,"initWithContentsOfURL:usedEncoding:error:")) return (Method)string_init_file_url_detect;
        }
    }
    return NULL;
}
