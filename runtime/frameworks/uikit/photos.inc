 
struct PhotoWrite { Word image,target; const char *selector; void *context; };
static void photo_write_finish(struct PhotoWrite *write,Word error) {
    if (write->target && write->selector) {
        Method callback=find_implementation(write->target,write->selector);
        if (!callback) stop("Missing photo save completion selector",write->selector);
        callback(write->target,write->selector,write->image,error,(Word)write->context,0);
    }
}
static void photo_write_release(struct PhotoWrite *write) {
    metal_object_release(write->image); metal_object_release(write->target); free(write);
}
static Word photo_write_error(long code,Word description) {
    struct GuestMap *info=(void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));
    map_set(info,"Photo save",description,copy_string("NSLocalizedDescription"));
    return error_with_domain(external_class("_OBJC_CLASS_$_NSError"),"Photo save",copy_string("NSCocoaErrorDomain"),code,(Word)info);
}
JNIEXPORT jstring JNICALL HOST_JNI(HostActivity_completePhotoWrite)(JNIEnv *env,jobject instance,jlong token,jlong code,jstring description) {
    struct PhotoWrite *write=(void *)(Word)token;
    if (!write) return NULL;
    may_unwind=1; snprintf(stage,sizeof(stage),"Original UIImage photo-save completion");
    int failed=setjmp(boundary);
    if (!failed) {
        Word error=code ? photo_write_error((long)code,description ? string_from_java(env,description,"Photo save") : copy_string("Photo could not be saved")) : 0;
        photo_write_finish(write,error);
    }
    photo_write_release(write); may_unwind=0;
    return failed ? (*env)->NewStringUTF(env,status) : NULL;
}
static void image_write_album(struct HostObject *image,Word target,const char *selector,void *context) {
    struct PhotoWrite *write=calloc(1,sizeof(*write));
    if (!write) stop("Allocation failed","UIImageWriteToSavedPhotosAlbum");
    *write=(struct PhotoWrite){metal_object_retain((Word)image),metal_object_retain(target),selector,context};
    Word representation=image_png(image);
    struct HostObject *data=(void *)representation;
    JNIEnv *env=attached_environment("UIImageWriteToSavedPhotosAlbum");
    jclass cls=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,cls,"saveGuestPhoto","(J[B)V");
    jbyteArray bytes=NULL;
    if (data && data->content[0]<=INT32_MAX) {
        bytes=(*env)->NewByteArray(env,(jsize)data->content[0]);
        if (bytes) (*env)->SetByteArrayRegion(env,bytes,0,(jsize)data->content[0],(const jbyte *)data_buffer(data));
    }
    if (!(*env)->ExceptionCheck(env)) (*env)->CallVoidMethod(env,activity,method,(jlong)write,bytes);
    if (bytes) (*env)->DeleteLocalRef(env,bytes);
    (*env)->DeleteLocalRef(env,cls);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env); photo_write_release(write);
        stop("Android photo-save dispatch failed","UIImageWriteToSavedPhotosAlbum");
    }
}
