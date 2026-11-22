 
struct GuestInsets { double top,left,bottom,right; };
static struct HostObject general_pasteboard;
struct NamedPasteboard { struct HostObject object; struct NamedPasteboard *next; };
static struct NamedPasteboard *named_pasteboards;
static pthread_mutex_t pasteboard_mutex=PTHREAD_MUTEX_INITIALIZER;
static int idle_timer_disabled;
static void ui_idle_timer(Word object,const char *selector,Word disabled) {
    if (idle_timer_disabled==(disabled!=0)) return;
    JNIEnv *env=attached_environment(selector); jclass cls=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,cls,"guestKeepScreenOn","(Z)V"); (*env)->CallVoidMethod(env,activity,method,(jboolean)(disabled!=0)); (*env)->DeleteLocalRef(env,cls);
    idle_timer_disabled=disabled!=0;
}
static Word ui_idle_state(void) { return idle_timer_disabled; }
static struct GuestInsets ui_safe_insets(Word object,const char *selector) {
    JNIEnv *env=attached_environment(selector); jclass cls=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,cls,"guestSafeInsets","()[I"); jintArray array=(*env)->CallObjectMethod(env,activity,method);
    jint inset[4]={0}; if (array && (*env)->GetArrayLength(env,array)==4) (*env)->GetIntArrayRegion(env,array,0,4,inset);
    if (array) (*env)->DeleteLocalRef(env,array); (*env)->DeleteLocalRef(env,cls);
    double scale=screen_scale(); struct ViewState *state=view_state(object); struct GuestRect screen=screen_bounds();
    double x=state->frame.x,y=state->frame.y;
    for (Word parent=state->parent;parent;parent=view_state(parent)->parent) { struct ViewState *p=view_state(parent); x+=p->frame.x-p->bounds.x; y+=p->frame.y-p->bounds.y; }
    return (struct GuestInsets){fmax(0,inset[0]/scale-y),fmax(0,inset[1]/scale-x),fmax(0,y+state->bounds.height-(screen.height-inset[2]/scale)),fmax(0,x+state->bounds.width-(screen.width-inset[3]/scale))};
}
static Word ui_url_string(Word url,const char *selector) {
    if (!url) return 0;
    Method absolute=find_implementation(url,"absoluteString"); return absolute ? absolute(url,"absoluteString",0,0,0,0) : 0;
}
static Word ui_open_url_mode(Word object,const char *selector,Word url,int query) {
    Word absolute=ui_url_string(url,selector); if (!absolute) return 0;
    JNIEnv *env=attached_environment(selector); jclass cls=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,cls,"guestOpenUrl","(Ljava/lang/String;Z)Z"); jstring text=http_java_string(env,absolute);
    Word result=(*env)->CallBooleanMethod(env,activity,method,text,(jboolean)query);
    if (text) (*env)->DeleteLocalRef(env,text); (*env)->DeleteLocalRef(env,cls);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return 0; } return result;
}
static Word ui_open_url(Word object,const char *selector,Word url) { return ui_open_url_mode(object,selector,url,0); }
static Word ui_can_open_url(Word object,const char *selector,Word url) { return ui_open_url_mode(object,selector,url,1); }
struct UrlCompletion { Word block; unsigned success; };
static void ui_url_completion(void *context) { struct UrlCompletion *c=context; ((void (*)(Word,unsigned char))read_word(c->block+16))(c->block,c->success); _Block_release((void *)c->block); free(c); }
static void ui_open_url_options(Word object,const char *selector,Word url,Word options,Word block) {
    unsigned success=ui_open_url_mode(object,selector,url,0);
    if (block) { struct UrlCompletion *c=calloc(1,sizeof(*c)); if (!c) stop("Allocation failed",selector); c->block=(Word)_Block_copy((void *)block); c->success=success; schedule_main_function(ui_url_completion,c); }
}
static Word ui_pasteboard(Word cls) { general_pasteboard.isa=cls; return (Word)&general_pasteboard; }
static Word ui_pasteboard_named(Word cls,const char *selector,Word name,unsigned char create) {
    if (!name) stop("Nil pasteboard name",selector);
    if (!strcmp(string_bytes(name,selector),"com.apple.UIKit.pboard.general")) return ui_pasteboard(cls);
    pthread_mutex_lock(&pasteboard_mutex);
    for (struct NamedPasteboard *board=named_pasteboards;board;board=board->next) {
        if (same_objects(board->object.content[0],name)) { pthread_mutex_unlock(&pasteboard_mutex); return (Word)&board->object; }
    }
    if (!create) { pthread_mutex_unlock(&pasteboard_mutex); return 0; }
    struct NamedPasteboard *board=calloc(1,sizeof(*board));
    if (!board) { pthread_mutex_unlock(&pasteboard_mutex); stop("Allocation failed",selector); }
    board->object.isa=cls; board->object.content[0]=text_copy(name,"copy");
    board->next=named_pasteboards; named_pasteboards=board;
    pthread_mutex_unlock(&pasteboard_mutex); return (Word)&board->object;
}
static Word ui_pasteboard_name(struct HostObject *board) {
    return board==&general_pasteboard ? copy_string("com.apple.UIKit.pboard.general") : board->content[0];
}
static void ui_pasteboard_remove(Word cls,const char *selector,Word name) {
    if (!name) stop("Nil pasteboard name",selector);
    pthread_mutex_lock(&pasteboard_mutex);
    struct NamedPasteboard **slot=&named_pasteboards;
    while (*slot && !same_objects((*slot)->object.content[0],name)) slot=&(*slot)->next;
    if (*slot) { struct NamedPasteboard *board=*slot; *slot=board->next; board->object.content[1]=0; ++board->object.content[3]; board->next=NULL; }
    pthread_mutex_unlock(&pasteboard_mutex);
}
static Word ui_pasteboard_text(Word object,const char *selector) {
    if (object!=(Word)&general_pasteboard) {
        pthread_mutex_lock(&pasteboard_mutex); Word text=((struct HostObject *)object)->content[1];
        pthread_mutex_unlock(&pasteboard_mutex); return text;
    }
    JNIEnv *env=attached_environment(selector); jclass cls=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,cls,"guestClipboardText","()Ljava/lang/String;"); jstring text=(*env)->CallObjectMethod(env,activity,method);
    Word result=text ? string_from_java(env,text,selector) : 0;
    if (text) (*env)->DeleteLocalRef(env,text); (*env)->DeleteLocalRef(env,cls); return result;
}
static void ui_pasteboard_set_text(Word object,const char *selector,Word string) {
    if (object!=(Word)&general_pasteboard) {
        Word text=string ? text_copy(string,"copy") : 0;
        pthread_mutex_lock(&pasteboard_mutex); struct HostObject *board=(void *)object;
        board->content[1]=text; ++board->content[3]; pthread_mutex_unlock(&pasteboard_mutex); return;
    }
    JNIEnv *env=attached_environment(selector); jclass cls=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,cls,"guestClipboardText","(Ljava/lang/String;)V"); jstring text=string ? http_java_string(env,string) : NULL;
    (*env)->CallVoidMethod(env,activity,method,text); if (text) (*env)->DeleteLocalRef(env,text); (*env)->DeleteLocalRef(env,cls);
}
static Word ui_pasteboard_has_text(Word object,const char *selector) { return ui_pasteboard_text(object,selector)!=0; }
static Method ui_platform_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver) return NULL;
    if (object==(Word)&application) {
        if (!strcmp(selector,"setIdleTimerDisabled:")) return (Method)ui_idle_timer;
        if (!strcmp(selector,"isIdleTimerDisabled") || !strcmp(selector,"idleTimerDisabled")) return (Method)ui_idle_state;
        if (!strcmp(selector,"openURL:")) return (Method)ui_open_url;
        if (!strcmp(selector,"canOpenURL:")) return (Method)ui_can_open_url;
        if (!strcmp(selector,"openURL:options:completionHandler:")) return (Method)ui_open_url_options;
    }
    if (!class_receiver && (!strcmp(receiver,"_OBJC_CLASS_$_UIView") || !strcmp(receiver,"_OBJC_CLASS_$_UILabel") || object==(Word)&root_view) && !strcmp(selector,"safeAreaInsets")) return (Method)ui_safe_insets;
    if (!strcmp(receiver,"_OBJC_CLASS_$_UIPasteboard")) {
        if (class_receiver && !strcmp(selector,"generalPasteboard")) return (Method)ui_pasteboard;
        if (class_receiver && !strcmp(selector,"pasteboardWithName:create:")) return (Method)ui_pasteboard_named;
        if (class_receiver && !strcmp(selector,"removePasteboardWithName:")) return (Method)ui_pasteboard_remove;
        if (!class_receiver && !strcmp(selector,"name")) return (Method)ui_pasteboard_name;
        if (!class_receiver && !strcmp(selector,"string")) return (Method)ui_pasteboard_text;
        if (!class_receiver && !strcmp(selector,"setString:")) return (Method)ui_pasteboard_set_text;
        if (!class_receiver && !strcmp(selector,"hasStrings")) return (Method)ui_pasteboard_has_text;
    }
    return NULL;
}
