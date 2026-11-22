 
struct ShareState { Word object,items,excluded,handler,present_completion,presenter; struct ShareState *next; };
static Word unsupported_service_error(const char *,int,const char *);
static struct ShareState *share_states;
static struct ShareState *share_state(Word object) {
    for (struct ShareState *s=share_states;s;s=s->next) if (s->object==object) return s;
    struct ShareState *s=calloc(1,sizeof(*s)); if (!s) stop("Allocation failed","UIActivityViewController");
    s->object=object; s->next=share_states; share_states=s; return s;
}
static Word ui_share_init(Word object,const char *selector,Word items,Word activities) {
    if (!items || activities) stop("Unsupported custom share items/activities",selector);
    share_state(object)->items=sequence_copy((void *)items,"copy"); return object;
}
static void ui_share_excluded(Word object,const char *selector,Word types) { share_state(object)->excluded=types ? sequence_copy((void *)types,"copy") : 0; }
static Word ui_share_get_excluded(Word object) { return share_state(object)->excluded; }
static void ui_share_handler(Word object,const char *selector,Word block) {
    struct ShareState *s=share_state(object); if (s->handler) _Block_release((void *)s->handler); s->handler=block ? (Word)_Block_copy((void *)block) : 0;
}
static Word ui_share_get_handler(Word object) { return share_state(object)->handler; }
static void ui_share_unsupported(void *context) {
    struct ShareState *s=context;
    Word error=unsupported_service_error("I2ACompatibilityErrorDomain",2,"Unsupported activity item");
    if (s->handler) ((void (*)(Word,Word,unsigned char,Word,Word))read_word(s->handler+16))(s->handler,0,0,0,error);
}
static void ui_share_present(Word presenter,const char *selector,Word object,Word completion) {
    struct ShareState *s=share_state(object);
    if (!s->items || s->presenter || view_state(presenter)->presented) stop("Invalid share presentation",selector);
    struct Sequence *items=(void *)s->items;
    Word text=mutable_string(external_class("_OBJC_CLASS_$_NSMutableString"));
    JNIEnv *env=attached_environment(selector);
    jclass cls=(*env)->GetObjectClass(env,activity),strings=(*env)->FindClass(env,"java/lang/String"),bytes_class=(*env)->FindClass(env,"[B");
    jobjectArray data=(*env)->NewObjectArray(env,(jsize)items->count,bytes_class,NULL);
    jobjectArray files=(*env)->NewObjectArray(env,(jsize)items->count,strings,NULL);
    unsigned text_count=0,unsupported=0;
    for (size_t i=0;i<items->count;++i) {
        Word item=items->items[i],string=0; const char *base=item ? external_base(read_word(item)) : NULL;
        if (base && (!strcmp(base,"_OBJC_CLASS_$_NSString") || !strcmp(base,"_OBJC_CLASS_$_NSMutableString"))) string=item;
        else if (base && !strcmp(base,"_OBJC_CLASS_$_NSURL")) {
            Word address=ui_url_string(item,selector);
            const char *value=string_bytes(address,selector);
            if (!strncasecmp(value,"file:",5)) {
                jstring path=http_java_string(env,url_path((void *)item));
                (*env)->SetObjectArrayElement(env,files,(jsize)i,path); if (path) (*env)->DeleteLocalRef(env,path);
            } else string=address;
        } else if (base && (!strcmp(base,"_OBJC_CLASS_$_NSData") || !strcmp(base,"_OBJC_CLASS_$_NSMutableData") || !strcmp(base,"_OBJC_CLASS_$_UIImage"))) {
            struct HostObject *representation=(void *)(!strcmp(base,"_OBJC_CLASS_$_UIImage") ? image_png((void *)item) : item);
            if (!representation || representation->content[0]>INT32_MAX) { unsupported=1; break; }
            jbyteArray bytes=(*env)->NewByteArray(env,(jsize)representation->content[0]);
            if (bytes) {
                (*env)->SetByteArrayRegion(env,bytes,0,(jsize)representation->content[0],(const jbyte *)data_buffer(representation));
                (*env)->SetObjectArrayElement(env,data,(jsize)i,bytes); (*env)->DeleteLocalRef(env,bytes);
            }
        } else { unsupported=1; break; }
        if (!string) continue;
        if (text_count++) append_string((void *)text,selector,copy_string("\n"));
        append_string((void *)text,selector,string);
    }
    if (unsupported) {
        (*env)->DeleteLocalRef(env,data); (*env)->DeleteLocalRef(env,files); (*env)->DeleteLocalRef(env,bytes_class);
        (*env)->DeleteLocalRef(env,strings); (*env)->DeleteLocalRef(env,cls);
        schedule_main_function(ui_share_unsupported,s); return;
    }
    s->presenter=presenter; s->present_completion=completion ? (Word)_Block_copy((void *)completion) : 0;
    view_state(presenter)->presented=object; view_state(object)->presenting=presenter;
    struct Sequence *excluded=(void *)s->excluded; size_t count=excluded ? excluded->count : 0;
    jobjectArray types=(*env)->NewObjectArray(env,count,strings,NULL);
    for (size_t i=0;i<count;++i) { jstring value=http_java_string(env,excluded->items[i]); (*env)->SetObjectArrayElement(env,types,i,value); if (value) (*env)->DeleteLocalRef(env,value); }
    jmethodID show=(*env)->GetMethodID(env,cls,"showGuestShare","(JLjava/lang/String;[[B[Ljava/lang/String;[Ljava/lang/String;)V"); jstring body=http_java_string(env,text);
    (*env)->CallVoidMethod(env,activity,show,(jlong)object,body,data,files,types);
    (*env)->DeleteLocalRef(env,data); (*env)->DeleteLocalRef(env,files); (*env)->DeleteLocalRef(env,bytes_class);
    if (body) (*env)->DeleteLocalRef(env,body); (*env)->DeleteLocalRef(env,types); (*env)->DeleteLocalRef(env,strings); (*env)->DeleteLocalRef(env,cls);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env); stop("Android share preparation failed",selector); }
}
static void ui_share_clear(struct ShareState *s) { if (s->presenter) view_state(s->presenter)->presented=0; view_state(s->object)->presenting=0; s->presenter=0; }
static void ui_share_dismiss(Word object,const char *selector,Word completion) {
    JNIEnv *env=attached_environment(selector); jclass cls=(*env)->GetObjectClass(env,activity);
    jmethodID dismiss=(*env)->GetMethodID(env,cls,"dismissGuestShare","(JJ)V");
    (*env)->CallVoidMethod(env,activity,dismiss,(jlong)object,(jlong)completion); (*env)->DeleteLocalRef(env,cls);
}
JNIEXPORT jstring JNICALL HOST_JNI(HostActivity_completeGuestShare)(JNIEnv *env,jobject instance,jlong token,jint result) {
    struct ShareState *s=share_state((Word)token); may_unwind=1; snprintf(stage,sizeof(stage),"Original share callback"); int failed=setjmp(boundary);
    if (!failed && result==-2) {
        Word completion=s->present_completion; s->present_completion=0;
        if (completion) { ((void (*)(Word))read_word(completion+16))(completion); _Block_release((void *)completion); }
    } else if (!failed) {
        ui_share_clear(s);
        if (s->present_completion) { _Block_release((void *)s->present_completion); s->present_completion=0; }
        Word error=result<0 ? unsupported_service_error("I2ACompatibilityErrorDomain",2,"No Android application can handle this share request") : 0;
        if (s->handler) ((void (*)(Word,Word,unsigned char,Word,Word))read_word(s->handler+16))(s->handler,0,result==1,0,error);
    }
    may_unwind=0; return failed ? (*env)->NewStringUTF(env,status) : NULL;
}
static Method ui_share_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (class_receiver || !receiver || strcmp(receiver,"_OBJC_CLASS_$_UIActivityViewController")) return NULL;
    if (!strcmp(selector,"initWithActivityItems:applicationActivities:")) return (Method)ui_share_init;
    if (!strcmp(selector,"setExcludedActivityTypes:")) return (Method)ui_share_excluded;
    if (!strcmp(selector,"excludedActivityTypes")) return (Method)ui_share_get_excluded;
    if (!strcmp(selector,"setCompletionWithItemsHandler:")) return (Method)ui_share_handler;
    if (!strcmp(selector,"completionWithItemsHandler")) return (Method)ui_share_get_handler;
    return NULL;
}
