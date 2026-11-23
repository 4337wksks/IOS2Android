struct BundleStream { int fd; int64_t base,length,position; };

static int bundle_stream_open(const char *path,struct BundleStream *stream) {
    const char *relative=bundle_relative(path);
    if (!BUNDLE_STREAMING || !bundle_asset_manager || !relative || !*relative) return 0;
    if (relative[0]=='/' || !strcmp(relative,"..") || !strncmp(relative,"../",3) ||
        strstr(relative,"/../") || !strcmp(strrchr(relative,'/') ? strrchr(relative,'/') : "","/..")) {
        errno=EACCES; return -1;
    }
    char *asset_path=NULL;
    if (asprintf(&asset_path,"%s/%s",GUEST_BUNDLE_NAME,relative)<0) { errno=ENOMEM; return -1; }
    AAsset *asset=AAssetManager_open(bundle_asset_manager,asset_path,AASSET_MODE_RANDOM);
    free(asset_path);
    if (!asset) { errno=ENOENT; return -1; }
    off64_t base,length;
    int fd=AAsset_openFileDescriptor64(asset,&base,&length);
    AAsset_close(asset);
    if (fd<0) return 0;
    if (base<0 || length<0 || base>INT64_MAX-length) { close(fd); errno=EOVERFLOW; return -1; }
    if (fcntl(fd,F_SETFD,FD_CLOEXEC)<0) { int code=errno; close(fd); errno=code; return -1; }
    *stream=(struct BundleStream){.fd=fd,.base=base,.length=length};
    return 1;
}

static int bundle_stream_read(void *cookie,char *bytes,int length) {
    struct BundleStream *stream=cookie;
    if (length<=0 || stream->position>=stream->length) return 0;
    int64_t left=stream->length-stream->position;
    if (length>left) length=(int)left;
    ssize_t count;
    do { count=pread(stream->fd,bytes,(size_t)length,stream->base+stream->position); }
    while (count<0 && errno==EINTR);
    if (count>0) stream->position+=count;
    return (int)count;
}

static fpos_t bundle_stream_seek(void *cookie,fpos_t offset,int whence) {
    struct BundleStream *stream=cookie;
    int64_t base;
    if (whence==SEEK_SET) base=0;
    else if (whence==SEEK_CUR) base=stream->position;
    else if (whence==SEEK_END) base=stream->length;
    else { errno=EINVAL; return -1; }
    if (offset < -base) { errno=EINVAL; return -1; }
    if (offset>INT64_MAX-base) { errno=EOVERFLOW; return -1; }
    stream->position=base+offset;
    return stream->position;
}

static int bundle_stream_close(void *cookie) {
    struct BundleStream *stream=cookie;
    int result=close(stream->fd); free(stream); return result;
}

static FILE *read_only_file(const char *path) {
    struct BundleStream opened;
    int direct=bundle_stream_open(path,&opened);
    if (direct<0) return NULL;
    if (direct) {
        struct BundleStream *stream=malloc(sizeof(*stream));
        if (!stream) { close(opened.fd); errno=ENOMEM; return NULL; }
        *stream=opened;
        FILE *file=funopen(stream,bundle_stream_read,NULL,bundle_stream_seek,bundle_stream_close);
        if (!file) { int code=errno; bundle_stream_close(stream); errno=code; }
        return file;
    }
    char *native=native_path(path);
    if (!native) return NULL;
    FILE *file=fopen(native,"rb"); free(native); return file;
}

static int read_only_stat(const char *path,struct stat *info) {
    struct BundleStream stream;
    int direct=bundle_stream_open(path,&stream);
    if (direct<0) return -1;
    if (direct) {
        int result=fstat(stream.fd,info),code=errno;
        close(stream.fd);
        if (!result) {
            info->st_size=stream.length; info->st_blocks=stream.length/512+(stream.length%512!=0);
            info->st_mode=S_IFREG|0444;
        }
        errno=code; return result;
    }
    char *native=native_path(path);
    if (!native) return -1;
    int result=stat(native,info),code=errno; free(native); errno=code; return result;
}
