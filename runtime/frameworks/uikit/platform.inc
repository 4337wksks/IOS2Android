 
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
struct WebState;
struct WebBinding { struct WebState *web;struct WebBinding *next; };
struct WebConfiguration { Word preferences,controller,suffix;Word media;struct WebBinding *bindings; };
struct ScrollState { struct WebState *web;struct GuestPoint offset;struct GuestSize size;struct GuestInsets insets;Word delegate;unsigned enabled,vertical,horizontal,directional,top,epoch,cached;Method callback; };
struct WebEvaluation { Word identifier,block;struct WebEvaluation *next; };
struct WebState {
    Word object,configuration,delegate,ui_delegate,request,navigation,url,title,user_agent,scroll;
    unsigned references,wk,back,forward,scales,epoch,cached,committed;_Atomic unsigned closed,loading;
    double progress;
    struct WebBinding preferences_binding,controller_binding,configuration_binding;
    struct WebEvaluation *evaluations;
    Method callbacks[6];
    unsigned synced;float metrics[11];int options[4];
};
static Word web_operation_sequence;
static struct WebConfiguration *web_configuration(Word object) {
    struct HostObject *host=(void *)object;
    if(!host->content[0]){struct WebConfiguration *configuration=calloc(1,sizeof(*configuration));if(!configuration)stop("Allocation failed","Web configuration");
        configuration->preferences=host_allocate(external_class("_OBJC_CLASS_$_WKPreferences"));((struct HostObject *)configuration->preferences)->content[0]=1;
        configuration->controller=host_allocate(external_class("_OBJC_CLASS_$_WKUserContentController"));((struct HostObject *)configuration->controller)->content[0]=sequence_empty(external_class("_OBJC_CLASS_$_NSMutableArray"));configuration->media=3;host->content[0]=(Word)configuration;}
    return (void *)host->content[0];
}
static struct ScrollState *scroll_state(Word object) {
    struct ViewState *view=view_state(object);if(!view->scroll){struct ScrollState *scroll=calloc(1,sizeof(*scroll));if(!scroll)stop("Allocation failed","UIScrollView");scroll->enabled=scroll->vertical=scroll->horizontal=scroll->top=1;view->scroll=scroll;}return view->scroll;
}
static Word web_error(const char *domain,int code,const char *description,Word url) {
    struct GuestMap *info=(void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));map_set(info,"Web error",copy_string(description),copy_string("NSLocalizedDescription"));if(url)map_set(info,"Web error",url,copy_string("NSErrorFailingURLKey"));return error_with_domain(external_class("_OBJC_CLASS_$_NSError"),"Web error",copy_string(domain),code,(Word)info);
}
static void web_command(struct WebState *web,const char *action,Word payload) {
    if(web->closed)return;JNIEnv *env=attached_environment("Web command");static jmethodID method;
    if(!method){jclass type=(*env)->GetObjectClass(env,activity);method=(*env)->GetMethodID(env,type,"webCommand","(JLjava/lang/String;Ljava/lang/String;)V");(*env)->DeleteLocalRef(env,type);}
    Word error=0;jstring input=payload ? ui_java_json(env,payload,&error) : NULL;if(payload && !input)stop("Invalid web command payload",action);
    jstring name=(*env)->NewStringUTF(env,action);(*env)->CallVoidMethod(env,activity,method,(jlong)web->object,name,input);(*env)->DeleteLocalRef(env,name);if(input)(*env)->DeleteLocalRef(env,input);
    if((*env)->ExceptionCheck(env)){(*env)->ExceptionClear(env);stop("Android web command failed",action);}
}
static void web_field(Word payload,const char *key,Word value) { if(value)map_set((void *)payload,"Web payload",value,copy_string(key)); }
static void web_update_configuration(struct WebState *web) {
    if(web->closed)return;struct WebConfiguration *configuration=web_configuration(web->configuration);struct HostObject *preferences=(void *)configuration->preferences,*controller=(void *)configuration->controller;
    Word payload=empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));web_field(payload,"javascript",number_bool(external_class("_OBJC_CLASS_$_NSNumber"),"Web JavaScript",preferences->content[0]));
    double minimum=0;memcpy(&minimum,&preferences->content[1],sizeof(minimum));web_field(payload,"minimumFont",number_integer((Word)ceil(minimum)));web_field(payload,"mediaGesture",number_bool(external_class("_OBJC_CLASS_$_NSNumber"),"Web media",configuration->media!=0));web_field(payload,"userAgent",web->user_agent);web_field(payload,"agentSuffix",configuration->suffix);
    Word scripts=sequence_empty(external_class("_OBJC_CLASS_$_NSArray"));struct Sequence *entries=(void *)controller->content[0];
    if(entries)for(size_t i=0;i<entries->count;++i){struct HostObject *script=(void *)entries->items[i];Word record=empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));web_field(record,"source",script->content[0]);web_field(record,"time",number_integer(script->content[1]));web_field(record,"main",number_bool(external_class("_OBJC_CLASS_$_NSNumber"),"Web script frame",script->content[2]));sequence_add((void *)scripts,"Web scripts",record);}
    web_field(payload,"scripts",scripts);web_command(web,"configure",payload);
}
static void web_binding_update(struct WebBinding *bindings) { for(struct WebBinding *binding=bindings;binding;binding=binding->next)web_update_configuration(binding->web); }
static void web_binding_remove(struct WebBinding **list,struct WebBinding *binding) { while(*list && *list!=binding)list=&(*list)->next;if(*list)*list=binding->next;binding->next=NULL; }
static void web_subscribe(struct WebState *web) {
    struct WebConfiguration *configuration=web_configuration(web->configuration);struct HostObject *preferences=(void *)configuration->preferences,*controller=(void *)configuration->controller;
    web->preferences_binding=(struct WebBinding){web,(void *)preferences->content[3]};preferences->content[3]=(Word)&web->preferences_binding;
    web->controller_binding=(struct WebBinding){web,(void *)controller->content[3]};controller->content[3]=(Word)&web->controller_binding;
    web->configuration_binding=(struct WebBinding){web,configuration->bindings};configuration->bindings=&web->configuration_binding;
}
static void web_unsubscribe(struct WebState *web) {
    struct WebConfiguration *configuration=web_configuration(web->configuration);struct HostObject *preferences=(void *)configuration->preferences,*controller=(void *)configuration->controller;
    struct WebBinding *bindings=(void *)preferences->content[3];web_binding_remove(&bindings,&web->preferences_binding);preferences->content[3]=(Word)bindings;
    bindings=(void *)controller->content[3];web_binding_remove(&bindings,&web->controller_binding);controller->content[3]=(Word)bindings;web_binding_remove(&configuration->bindings,&web->configuration_binding);
}
static Word web_configuration_copy(Word object,const char *selector,Word zone) { Word copy=host_allocate(read_word(object));struct WebConfiguration *configuration=web_configuration(copy);*configuration=*web_configuration(object);configuration->bindings=NULL;return copy; }
static struct WebState *web_state(Word object) {
    struct ViewState *view=view_state(object);if(!view->web){struct WebState *web=calloc(1,sizeof(*web));if(!web)stop("Allocation failed","Web view");web->object=object;web->references=1;web->wk=class_inherits(read_word(object),external_class("_OBJC_CLASS_$_WKWebView"));web->configuration=host_allocate(external_class("_OBJC_CLASS_$_WKWebViewConfiguration"));web_configuration(web->configuration);web->scroll=host_allocate(external_class("_OBJC_CLASS_$_UIScrollView"));scroll_state(web->scroll)->web=web;view_state(web->scroll)->parent=object;view->web=web;web_subscribe(web);web_update_configuration(web);}return view->web;
}
static void web_state_release(struct WebState *web) { if(__atomic_sub_fetch(&web->references,1,__ATOMIC_ACQ_REL)==0)free(web); }
static Word web_init(Word object,const char *selector,struct GuestRect frame,Word configuration) {
    view_init_frame(object,selector,frame);struct WebState *web=web_state(object);
    if(configuration){web_unsubscribe(web);web->configuration=web_configuration_copy(configuration,"copy",0);web_subscribe(web);web_update_configuration(web);}ui_web_sync(object);return object;
}
static Word web_init_frame(Word object,const char *selector,struct GuestRect frame) { return web_init(object,selector,frame,0); }
static Word web_init_plain(Word object,const char *selector) { return web_init(object,selector,(struct GuestRect){0},0); }
static void ui_web_sync(Word object) {
    struct ViewState *view=view_state(object);struct WebState *web=view->web;if(!web || web->closed)return;float metrics[11];int options[4];view_host_geometry(object,metrics,options);options[3]=(int)label_argb(view->background,0xffffffff);
    if(web->synced && !memcmp(web->metrics,metrics,sizeof(metrics)) && !memcmp(web->options,options,sizeof(options)))return;memcpy(web->metrics,metrics,sizeof(metrics));memcpy(web->options,options,sizeof(options));web->synced=1;
    struct ViewState *scroll=view_state(web->scroll);scroll->frame=(struct GuestRect){0,0,view->frame.width,view->frame.height};scroll->bounds.width=view->frame.width;scroll->bounds.height=view->frame.height;
    JNIEnv *env=attached_environment("Web geometry");static jmethodID method;if(!method){jclass type=(*env)->GetObjectClass(env,activity);method=(*env)->GetMethodID(env,type,"syncGuestWeb","(J[F[I)V");(*env)->DeleteLocalRef(env,type);}
    jfloatArray m=(*env)->NewFloatArray(env,COUNT(metrics));jintArray o=(*env)->NewIntArray(env,COUNT(options));(*env)->SetFloatArrayRegion(env,m,0,COUNT(metrics),metrics);(*env)->SetIntArrayRegion(env,o,0,COUNT(options),options);(*env)->CallVoidMethod(env,activity,method,(jlong)object,m,o);(*env)->DeleteLocalRef(env,m);(*env)->DeleteLocalRef(env,o);
    if((*env)->ExceptionCheck(env)){(*env)->ExceptionClear(env);stop("Android web geometry failed","Web view");}
}
static Word web_host_url(Word url) {
    if(!url)return copy_string("about:blank");struct HostObject *value=(void *)url;
    if(value->content[1]){const char *relative=bundle_relative(string_bytes(url_path(value),"Web file URL"));if(relative){Word prefix=copy_string("https://appassets.androidplatform.net/bundle/");return append_text(prefix,"Web file URL",copy_string(relative));}}
    return ui_url_string(url,"Web URL");
}
static Word web_guest_url(Word text) {
    Word url=url_from_string(external_class("_OBJC_CLASS_$_NSURL"),"Web URL",text);
    const char *bytes=string_bytes(text,"Web URL"),*prefix="https://appassets.androidplatform.net/bundle/";
    if(!strncmp(bytes,prefix,strlen(prefix))){const char *path=string_bytes(url_path((void *)url),"Web bundle URL");if(!strncmp(path,"/bundle/",8))return file_url(append_text(copy_string("/" GUEST_BUNDLE_NAME "/"),"Web bundle URL",copy_string(path+8)));}
    return url;
}
static Word web_navigation_new(struct WebState *web) { web->committed=0;web->navigation=host_allocate(external_class("_OBJC_CLASS_$_WKNavigation"));return web->navigation; }
static void web_delegate_cache(struct WebState *web) {
    if(web->cached && web->epoch==method_cache_epoch)return;const char *wk[]={"webView:didStartProvisionalNavigation:","webView:didReceiveServerRedirectForProvisionalNavigation:","webView:didCommitNavigation:","webView:didFinishNavigation:","webView:didFailProvisionalNavigation:withError:","webView:didFailNavigation:withError:"};const char *legacy[]={"webViewDidStartLoad:",NULL,NULL,"webViewDidFinishLoad:","webView:didFailLoadWithError:","webView:didFailLoadWithError:"};
    for(unsigned i=0;i<COUNT(wk);++i)web->callbacks[i]=web->delegate && (web->wk || legacy[i]) ? find_implementation(web->delegate,web->wk ? wk[i] : legacy[i]) : NULL;web->cached=1;web->epoch=method_cache_epoch;
}
static void web_notify(struct WebState *web,unsigned kind,Word error) {
    web_delegate_cache(web);Method method=web->callbacks[kind];if(!method)return;const char *wk[]={"webView:didStartProvisionalNavigation:","webView:didReceiveServerRedirectForProvisionalNavigation:","webView:didCommitNavigation:","webView:didFinishNavigation:","webView:didFailProvisionalNavigation:withError:","webView:didFailNavigation:withError:"};const char *legacy[]={"webViewDidStartLoad:",NULL,NULL,"webViewDidFinishLoad:","webView:didFailLoadWithError:","webView:didFailLoadWithError:"};
    method(web->delegate,web->wk ? wk[kind] : legacy[kind],web->object,web->wk ? web->navigation : error,web->wk ? error : 0,0);
}
struct WebReply { unsigned references,answered;struct WebState *web;Word payload,dialog,navigation;const char *action; };
struct WebReplyBlock { struct Block_layout layout;struct WebReply *reply; };
static void web_reply_release(struct WebReply *reply) { if(__atomic_sub_fetch(&reply->references,1,__ATOMIC_ACQ_REL)==0){web_state_release(reply->web);free(reply);} }
static void web_reply_copy(void *destination,const void *source) { struct WebReply *reply=((const struct WebReplyBlock *)source)->reply;((struct WebReplyBlock *)destination)->reply=reply;__atomic_add_fetch(&reply->references,1,__ATOMIC_RELAXED); }
static void web_reply_dispose(const void *block) { web_reply_release(((const struct WebReplyBlock *)block)->reply); }
static struct WebReplyBlock web_reply_block(struct WebReply *reply,void (*invoke)(void *,...)) {
    static struct { uintptr_t reserved,size;void (*copy)(void *,const void *);void (*dispose)(const void *); } descriptor={0,sizeof(struct WebReplyBlock),web_reply_copy,web_reply_dispose};
    return (struct WebReplyBlock){.layout={.isa=_NSConcreteStackBlock,.flags=BLOCK_HAS_COPY_DISPOSE,.invoke=invoke,.descriptor=(void *)&descriptor},.reply=reply};
}
static struct WebReply *web_reply(struct WebState *web) { struct WebReply *reply=calloc(1,sizeof(*reply));if(!reply)stop("Allocation failed","Web completion");reply->references=1;reply->web=web;__atomic_add_fetch(&web->references,1,__ATOMIC_RELAXED);return reply; }
static void web_policy_reply(struct WebReplyBlock *block,Word policy) {
    struct WebReply *reply=block->reply;if(__atomic_exchange_n(&reply->answered,1,__ATOMIC_ACQ_REL) || reply->web->closed)return;
    if(reply->web->navigation!=reply->navigation)return;
    if(policy==1 || policy==3)web_command(reply->web,reply->action,reply->payload);else reply->web->loading=0;
}
static Word web_load_payload(struct WebState *web,Word request,const char *action,Word payload,long kind) {
    web->request=request_copy((void *)request,"copy");web->url=request_url((void *)request);web_navigation_new(web);web->loading=1;web->progress=0;
    if(!web->wk){Method method=web->delegate ? find_implementation(web->delegate,"webView:shouldStartLoadWithRequest:navigationType:") : NULL;if(method && !(method(web->delegate,"webView:shouldStartLoadWithRequest:navigationType:",web->object,request,(Word)kind,0)&255)){web->loading=0;return web->navigation;}web_command(web,action,payload);return web->navigation;}
    Method method=web->delegate ? find_implementation(web->delegate,"webView:decidePolicyForNavigationAction:decisionHandler:") : NULL;
    if(!method){web_command(web,action,payload);return web->navigation;}
    struct HostObject *target=(void *)host_allocate(external_class("_OBJC_CLASS_$_WKFrameInfo"));target->content[0]=request;target->content[1]=1;
    struct HostObject *navigation=(void *)host_allocate(external_class("_OBJC_CLASS_$_WKNavigationAction"));navigation->content[0]=request;navigation->content[1]=(Word)kind;navigation->content[2]=navigation->content[3]=(Word)target;
    Word result=web->navigation;struct WebReply *reply=web_reply(web);reply->payload=payload;reply->action=action;reply->navigation=result;struct WebReplyBlock block=web_reply_block(reply,(void (*)(void *,...))web_policy_reply);
    struct GuestCallBoundary call={.previous=active_guest_call_boundary};active_guest_call_boundary=&call;int failed=setjmp(call.environment);if(!failed)method(web->delegate,"webView:decidePolicyForNavigationAction:decisionHandler:",web->object,(Word)navigation,(Word)&block,0);active_guest_call_boundary=call.previous;web_reply_release(reply);
    if(failed){char failure[sizeof(status)];memcpy(failure,status,sizeof(failure));stop("Web navigation callback failed",failure);}return result;
}
static Word web_load_request(Word object,const char *selector,Word request) {
    if(!request)stop("Web request is required",selector);struct WebState *web=web_state(object);struct Request *source=(void *)request;Word payload=empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));web_field(payload,"url",web_host_url(source->url));web_field(payload,"method",source->method ? source->method : copy_string("GET"));web_field(payload,"headers",source->headers);
    if(source->body_stream)stop("Streaming web request bodies are unavailable",selector);if(source->body)web_field(payload,"bytes",data_base64((void *)source->body,selector,0));return web_load_payload(web,request,"load",payload,5);
}
static Word web_load_data(Word object,const char *selector,Word data,Word mime,Word encoding,Word base) {
    if(!data || !mime || !encoding)stop("Web data, MIME type and encoding are required",selector);struct WebState *web=web_state(object);Word payload=empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));web_field(payload,"url",web_host_url(base));web_field(payload,"bytes",data_base64((void *)data,selector,0));web_field(payload,"mime",mime);web_field(payload,"encoding",encoding);
    Word url=base ? base : url_from_string(external_class("_OBJC_CLASS_$_NSURL"),selector,copy_string("about:blank"));return web_load_payload(web,request_basic(external_class("_OBJC_CLASS_$_NSURLRequest"),selector,url),"data",payload,5);
}
static Word web_load_html(Word object,const char *selector,Word text,Word base) { if(!text)stop("Web HTML is required",selector);Word data=encoded_string(text,selector,4);return web_load_data(object,selector,data,copy_string("text/html"),copy_string("UTF-8"),base); }
static void web_evaluate(Word object,const char *selector,Word source,Word block) {
    if(!source)stop("JavaScript source is required",selector);struct WebState *web=web_state(object);struct WebEvaluation *evaluation=NULL;Word identifier=__atomic_add_fetch(&web_operation_sequence,1,__ATOMIC_RELAXED);
    if(block){evaluation=malloc(sizeof(*evaluation));if(!evaluation)stop("Allocation failed",selector);*evaluation=(struct WebEvaluation){identifier,(Word)_Block_copy((void *)block),web->evaluations};web->evaluations=evaluation;}
    Word payload=empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));web_field(payload,"source",source);web_field(payload,"operation",number_integer(identifier));web_command(web,"evaluate",payload);
}
static void web_stop(Word object,const char *selector) { struct WebState *web=web_state(object);web->loading=0;web_command(web,"stop",0); }
static Word web_reload(Word object,const char *selector) { struct WebState *web=web_state(object);return web->request ? web_load_request(object,selector,web->request) : 0; }
static Word web_history(Word object,const char *selector) { struct WebState *web=web_state(object);int back=!strcmp(selector,"goBack");if(back ? !web->back : !web->forward)return 0;web_navigation_new(web);web->loading=1;web_command(web,back ? "back" : "forward",0);return web->navigation; }
static Word web_property(Word object,const char *selector) {
    struct WebState *web=web_state(object);if(!strcmp(selector,"URL"))return web->url;if(!strcmp(selector,"title"))return web->title;if(!strcmp(selector,"request"))return web->request;
    if(!strcmp(selector,"navigationDelegate") || !strcmp(selector,"delegate"))return web->delegate;if(!strcmp(selector,"UIDelegate"))return web->ui_delegate;if(!strcmp(selector,"configuration"))return web->configuration;if(!strcmp(selector,"scrollView"))return web->scroll;if(!strcmp(selector,"customUserAgent"))return web->user_agent;
    if(!strcmp(selector,"canGoBack"))return web->back;if(!strcmp(selector,"canGoForward"))return web->forward;if(!strcmp(selector,"scalesPageToFit"))return web->scales;return web->loading;
}
static double web_progress(Word object) { return web_state(object)->progress; }
static void web_set_property(Word object,const char *selector,Word value) {
    struct WebState *web=web_state(object);if(!strcmp(selector,"setNavigationDelegate:") || !strcmp(selector,"setDelegate:")){web->delegate=value;web->cached=0;}
    else if(!strcmp(selector,"setUIDelegate:"))web->ui_delegate=value;
    else if(!strcmp(selector,"setCustomUserAgent:")){web->user_agent=value ? text_copy(value,"copy") : 0;web_update_configuration(web);}
    else{web->scales=value!=0;Word payload=empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));web_field(payload,"scales",number_bool(external_class("_OBJC_CLASS_$_NSNumber"),selector,value));web_command(web,"viewport",payload);}
}
static Word web_configuration_property(Word object,const char *selector) { struct WebConfiguration *configuration=web_configuration(object);if(!strcmp(selector,"preferences"))return configuration->preferences;if(!strcmp(selector,"userContentController"))return configuration->controller;if(!strcmp(selector,"applicationNameForUserAgent"))return configuration->suffix;if(!strcmp(selector,"mediaTypesRequiringUserActionForPlayback"))return configuration->media;return 0; }
static void web_configuration_set(Word object,const char *selector,Word value) {
    struct WebConfiguration *configuration=web_configuration(object);
    if(!strcmp(selector,"setDataDetectorTypes:")){if(value)stop("Web data detection flags are unavailable",selector);return;}
    if(!strcmp(selector,"setMediaTypesRequiringUserActionForPlayback:")){if(value!=0 && value!=3)stop("Separate audio/video autoplay policies are unavailable",selector);configuration->media=value;}
    else if(!strcmp(selector,"setApplicationNameForUserAgent:"))configuration->suffix=value ? text_copy(value,"copy") : 0;
    else{if(!value)stop("Web configuration object is required",selector);size_t count=0;for(struct WebBinding *binding=configuration->bindings;binding;binding=binding->next)++count;struct WebState **views=count ? malloc(count*sizeof(*views)) : NULL;if(count && !views)stop("Allocation failed",selector);size_t index=0;for(struct WebBinding *binding=configuration->bindings;binding;binding=binding->next)views[index++]=binding->web;for(size_t i=0;i<count;++i)web_unsubscribe(views[i]);if(!strcmp(selector,"setPreferences:"))configuration->preferences=value;else configuration->controller=value;for(size_t i=0;i<count;++i)web_subscribe(views[i]);free(views);}
    web_binding_update(configuration->bindings);
}
static Word web_model_init(Word object,const char *selector) {
    const char *base=external_base(read_word(object));struct HostObject *host=(void *)object;
    if(!strcmp(base,"_OBJC_CLASS_$_WKWebViewConfiguration"))web_configuration(object);else if(!strcmp(base,"_OBJC_CLASS_$_WKPreferences"))host->content[0]=1;else if(!strcmp(base,"_OBJC_CLASS_$_WKUserContentController"))host->content[0]=sequence_empty(external_class("_OBJC_CLASS_$_NSMutableArray"));return object;
}
static void web_preference_set(Word object,const char *selector,Word value) { struct HostObject *host=(void *)object;if(!strcmp(selector,"setJavaScriptCanOpenWindowsAutomatically:")){if(value)stop("Automatic web windows are unavailable",selector);host->content[2]=0;}else host->content[0]=value!=0;web_binding_update((void *)host->content[3]); }
static Word web_preference_get(struct HostObject *object,const char *selector) { return !strcmp(selector,"javaScriptEnabled") ? object->content[0] : object->content[2]; }
static void web_minimum_font(Word object,const char *selector,double size) { if(!isfinite(size) || size<0 || size>INT32_MAX)stop("Invalid web minimum font size",selector);struct HostObject *host=(void *)object;memcpy(&host->content[1],&size,sizeof(size));web_binding_update((void *)host->content[3]); }
static double web_minimum_font_get(struct HostObject *object) { double size;memcpy(&size,&object->content[1],sizeof(size));return size; }
static Word web_script_init(Word object,const char *selector,Word source,Word time,unsigned char main) { if(!source || time>1)stop("Invalid web user script",selector);struct HostObject *script=(void *)object;script->content[0]=text_copy(source,"copy");script->content[1]=time;script->content[2]=main!=0;return object; }
static Word web_record_property(struct HostObject *object,const char *selector) { if(!strcmp(selector,"source") || !strcmp(selector,"request"))return object->content[0];if(!strcmp(selector,"injectionTime") || !strcmp(selector,"navigationType") || !strcmp(selector,"isMainFrame"))return object->content[1];if(!strcmp(selector,"sourceFrame"))return object->content[3];return object->content[2]; }
static void web_add_script(struct HostObject *object,const char *selector,Word script) { if(!script)stop("Web script is required",selector);if(!object->content[0])object->content[0]=sequence_empty(external_class("_OBJC_CLASS_$_NSMutableArray"));sequence_add((void *)object->content[0],selector,script);web_binding_update((void *)object->content[3]); }
static void web_remove_scripts(struct HostObject *object,const char *selector) { object->content[0]=sequence_empty(external_class("_OBJC_CLASS_$_NSMutableArray"));web_binding_update((void *)object->content[3]); }
static Word web_scripts(struct HostObject *object,const char *selector) { return object->content[0] ? sequence_copy((void *)object->content[0],"copy") : sequence_empty(external_class("_OBJC_CLASS_$_NSArray")); }
static void web_dialog_answer(struct WebReplyBlock *block,Word answer) {
    struct WebReply *reply=block->reply;if(__atomic_exchange_n(&reply->answered,1,__ATOMIC_ACQ_REL) || reply->web->closed)return;Word payload=empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));web_field(payload,"id",number_integer(reply->dialog));web_field(payload,"accepted",number_bool(external_class("_OBJC_CLASS_$_NSNumber"),"Web dialog",answer));if(reply->action && !strcmp(reply->action,"prompt") && answer)web_field(payload,"value",answer);web_command(reply->web,"dialog",payload);
}
static void web_dialog_alert(struct WebReplyBlock *block) { web_dialog_answer(block,1); }
static int web_dialog(struct WebState *web,unsigned kind,Word identifier,struct GuestMap *data) {
    const char *selector=kind==11 ? "webView:runJavaScriptAlertPanelWithMessage:initiatedByFrame:completionHandler:" : kind==12 ? "webView:runJavaScriptConfirmPanelWithMessage:initiatedByFrame:completionHandler:" : "webView:runJavaScriptTextInputPanelWithPrompt:defaultText:initiatedByFrame:completionHandler:";
    Method method=web->ui_delegate ? find_implementation(web->ui_delegate,selector) : NULL;if(!method)return 0;struct HostObject *frame=(void *)host_allocate(external_class("_OBJC_CLASS_$_WKFrameInfo"));frame->content[0]=web->request;frame->content[1]=1;Word message=map_get(data,selector,copy_string("message")),value=map_get(data,selector,copy_string("value"));
    struct WebReply *reply=web_reply(web);reply->dialog=identifier;reply->action=kind==13 ? "prompt" : NULL;struct WebReplyBlock block=web_reply_block(reply,(void (*)(void *,...))(kind==11 ? (void *)web_dialog_alert : (void *)web_dialog_answer));
    struct GuestCallBoundary call={.previous=active_guest_call_boundary};active_guest_call_boundary=&call;int failed=setjmp(call.environment);
    if(!failed){if(kind==13)((void (*)(Word,const char *,Word,Word,Word,Word,Word))method)(web->ui_delegate,selector,web->object,message,value,(Word)frame,(Word)&block);else method(web->ui_delegate,selector,web->object,message,(Word)frame,(Word)&block);}
    active_guest_call_boundary=call.previous;web_reply_release(reply);if(failed){char failure[sizeof(status)];memcpy(failure,status,sizeof(failure));stop("Web dialog callback failed",failure);}return 1;
}
static void web_dealloc(Word object,const char *selector) {
    struct WebState *web=view_state(object)->web;if(!web)return;web_command(web,"close",0);web->closed=1;web_unsubscribe(web);view_state(object)->web=NULL;scroll_state(web->scroll)->web=NULL;view_state(web->scroll)->parent=0;
    while(web->evaluations){struct WebEvaluation *evaluation=web->evaluations;web->evaluations=evaluation->next;ui_complete_callback(evaluation->block,0,web_error("NSURLErrorDomain",-999,"The web view was closed.",web->url),1);_Block_release((void *)evaluation->block);free(evaluation);}web_state_release(web);
}
static struct GuestPoint scroll_offset(Word object) { return scroll_state(object)->offset; }
static struct GuestSize scroll_size(Word object) { return scroll_state(object)->size; }
static struct GuestInsets scroll_insets(Word object) { return scroll_state(object)->insets; }
static void ui_scroll_input_delegate(Word object,Word delegate){struct ScrollState *scroll=scroll_state(object);scroll->delegate=delegate;scroll->cached=0;}
static void ui_scroll_input_options(Word object,float *metrics,int *options){struct ScrollState *scroll=scroll_state(object);double scale=metrics[18];metrics[19]=scroll->insets.left*scale;metrics[20]=scroll->insets.top*scale;metrics[12]+=metrics[19];metrics[13]+=metrics[20];metrics[14]+=scroll->insets.right*scale;metrics[15]+=scroll->insets.bottom*scale;metrics[16]=(scroll->offset.x+scroll->insets.left)*scale;metrics[17]=(scroll->offset.y+scroll->insets.top)*scale;options[17]=scroll->enabled;options[18]=scroll->vertical;options[19]=scroll->horizontal;options[20]=scroll->directional;options[21]=scroll->top;}
static void scroll_notify(Word object) {
    struct ScrollState *scroll=scroll_state(object);if(!scroll->delegate)return;
    if(!scroll->cached || scroll->epoch!=method_cache_epoch){scroll->callback=find_implementation(scroll->delegate,"scrollViewDidScroll:");scroll->epoch=method_cache_epoch;scroll->cached=1;}
    if(scroll->callback)scroll->callback(scroll->delegate,"scrollViewDidScroll:",object,0,0,0);
}
static void scroll_apply(Word object) {
    struct ScrollState *scroll=scroll_state(object);struct ViewState *view=view_state(object);view->bounds.x=scroll->offset.x;view->bounds.y=scroll->offset.y;view_sync_subtree(object);
    if(scroll->web){Word payload=empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));const char *keys[]={"x","y","top","left","bottom","right"};double values[]={scroll->offset.x,scroll->offset.y,scroll->insets.top,scroll->insets.left,scroll->insets.bottom,scroll->insets.right};for(unsigned i=0;i<COUNT(keys);++i)web_field(payload,keys[i],real_number_factory(external_class("_OBJC_CLASS_$_NSNumber"),"Scroll values",values[i]));web_field(payload,"enabled",number_bool(external_class("_OBJC_CLASS_$_NSNumber"),"Scroll state",scroll->enabled));web_field(payload,"vertical",number_bool(external_class("_OBJC_CLASS_$_NSNumber"),"Scroll state",scroll->vertical));web_field(payload,"horizontal",number_bool(external_class("_OBJC_CLASS_$_NSNumber"),"Scroll state",scroll->horizontal));web_command(scroll->web,"scroll",payload);}
}
static void scroll_set_offset(Word object,const char *selector,struct GuestPoint point) { if(!isfinite(point.x) || !isfinite(point.y))stop("Invalid scroll offset",selector);struct ScrollState *scroll=scroll_state(object);if(!memcmp(&scroll->offset,&point,sizeof(point)))return;scroll->offset=point;scroll_apply(object);scroll_notify(object); }
static void scroll_set_size(Word object,const char *selector,struct GuestSize size) { if(!isfinite(size.width) || !isfinite(size.height) || size.width<0 || size.height<0)stop("Invalid scroll content size",selector);struct ScrollState *scroll=scroll_state(object);if(scroll->web)stop("Web content size is controlled by the document",selector);scroll->size=size; }
static void scroll_set_insets(Word object,const char *selector,struct GuestInsets insets) { if(!isfinite(insets.top) || !isfinite(insets.left) || !isfinite(insets.bottom) || !isfinite(insets.right))stop("Invalid scroll insets",selector);scroll_state(object)->insets=insets;scroll_apply(object); }
static void scroll_set_property(Word object,const char *selector,Word value) { struct ScrollState *scroll=scroll_state(object);if(!strcmp(selector,"setDelegate:")){scroll->delegate=value;scroll->cached=0;}else if(!strcmp(selector,"setScrollEnabled:"))scroll->enabled=value!=0;else if(!strcmp(selector,"setShowsVerticalScrollIndicator:"))scroll->vertical=value!=0;else if(!strcmp(selector,"setShowsHorizontalScrollIndicator:"))scroll->horizontal=value!=0;else if(!strcmp(selector,"setDirectionalLockEnabled:"))scroll->directional=value!=0;else scroll->top=value!=0;scroll_apply(object); }
static Word scroll_property(Word object,const char *selector) { struct ScrollState *scroll=scroll_state(object);if(!strcmp(selector,"delegate"))return scroll->delegate;if(!strcmp(selector,"isScrollEnabled"))return scroll->enabled;if(!strcmp(selector,"showsVerticalScrollIndicator"))return scroll->vertical;if(!strcmp(selector,"showsHorizontalScrollIndicator"))return scroll->horizontal;if(!strcmp(selector,"isDirectionalLockEnabled"))return scroll->directional;return scroll->top; }
JNIEXPORT void JNICALL HOST_JNI(HostActivity_editorScroll)(JNIEnv *env,jobject instance,jlong token,jfloat x,jfloat y,jfloat width,jfloat height){struct ViewState *view=view_state(token);struct TextFieldState *field=view->input;if(!field || field->closed)return;may_unwind=1;int failed=setjmp(boundary);if(!failed){struct ScrollState *scroll=scroll_state(token);int changed=scroll->offset.x!=x || scroll->offset.y!=y;scroll->size=(struct GuestSize){width,height};scroll->offset=(struct GuestPoint){x,y};view->bounds.x=x;view->bounds.y=y;if(changed){view_sync_subtree(token);scroll_notify(token);}}may_unwind=0;if(failed)input_report_failure(env);}
JNIEXPORT jlong JNICALL HOST_JNI(HostActivity_webViewEvent)(JNIEnv *env,jobject instance,jlong token,jint kind,jlong operation,jstring input) {
    struct WebState *web=view_state((Word)token)->web;if(!web || web->closed)return 0;may_unwind=1;int failed=setjmp(boundary);Word handled=0;
    if(!failed){Word text=input ? string_from_java(env,input,"Web event") : copy_string("{}"),bytes=encoded_string(text,"Web event",4),error=0;Word value=json_parse(0,"Web event",(void *)bytes,4,&error);struct GuestMap *data=value && value!=(Word)&null_value ? (void *)value : NULL;
        if(kind==9){struct WebEvaluation **slot=&web->evaluations;while(*slot && (*slot)->identifier!=(Word)operation)slot=&(*slot)->next;if(*slot){struct WebEvaluation *evaluation=*slot;*slot=evaluation->next;Word result=data ? map_get(data,"JavaScript",copy_string("value")) : 0,message=data ? map_get(data,"JavaScript",copy_string("error")) : 0,code=data ? map_get(data,"JavaScript",copy_string("code")) : 0;Word failure=message ? web_error("WKErrorDomain",code ? (int)numeric_integer((void *)code) : 4,string_bytes(message,"JavaScript"),web->url) : 0;ui_complete_callback(evaluation->block,result==(Word)&null_value ? 0 : result,failure,1);_Block_release((void *)evaluation->block);free(evaluation);}}
        else if(!data)stop("Invalid web event data","Android WebView");
        else if(kind==0){Word url=map_get(data,"Web navigation",copy_string("url")),type=map_get(data,"Web navigation",copy_string("type"));if(url){Word request=request_basic(external_class("_OBJC_CLASS_$_NSURLRequest"),"Web navigation",web_guest_url(url));Word payload=empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));web_field(payload,"url",url);web_load_payload(web,request,"load",payload,type ? (long)numeric_integer((void *)type) : 5);}handled=1;}
        else if(kind>=1 && kind<=4){Word url=map_get(data,"Web URL",copy_string("url"));if(url)web->url=web_guest_url(url);web->loading=kind!=4;if(kind==3)web->committed=1;if(kind==4)web->progress=1;web_notify(web,(unsigned)kind-1,0);}
        else if(kind==5){Word message=map_get(data,"Web failure",copy_string("description")),code=map_get(data,"Web failure",copy_string("code")),url=map_get(data,"Web failure",copy_string("url"));if(url)web->url=web_guest_url(url);web->loading=0;web_notify(web,web->committed ? 5 : 4,web_error("NSURLErrorDomain",code ? (int)numeric_integer((void *)code) : -1,message ? string_bytes(message,"Web failure") : "Unable to load this page.",web->url));}
        else if(kind==6)web->title=map_get(data,"Web title",copy_string("title"));
        else if(kind==7){Word progress=map_get(data,"Web progress",copy_string("progress"));if(progress)web->progress=numeric_double((void *)progress);}
        else if(kind==8){struct ScrollState *scroll=scroll_state(web->scroll);const char *keys[]={"x","y","width","height"};double values[4]={scroll->offset.x,scroll->offset.y,scroll->size.width,scroll->size.height};for(unsigned i=0;i<COUNT(keys);++i){Word number=map_get(data,"Web scroll",copy_string(keys[i]));if(number)values[i]=numeric_double((void *)number);}int changed=values[0]!=scroll->offset.x || values[1]!=scroll->offset.y;scroll->offset=(struct GuestPoint){values[0],values[1]};scroll->size=(struct GuestSize){values[2],values[3]};struct ViewState *view=view_state(web->scroll);view->bounds.x=values[0];view->bounds.y=values[1];if(changed)view_sync_subtree(web->scroll);
            Word back=map_get(data,"Web history",copy_string("back")),forward=map_get(data,"Web history",copy_string("forward"));web->back=back && numeric_bool((void *)back);web->forward=forward && numeric_bool((void *)forward);
            if(changed)scroll_notify(web->scroll);}
        else if(kind==10){Method method=web->ui_delegate ? find_implementation(web->ui_delegate,"webViewDidClose:") : NULL;if(method)method(web->ui_delegate,"webViewDidClose:",web->object,0,0,0);}
        else if(kind>=11 && kind<=13)handled=web_dialog(web,(unsigned)kind,(Word)operation,data);
        else if(kind==15){web->loading=web->back=web->forward=0;while(web->evaluations){struct WebEvaluation *evaluation=web->evaluations;web->evaluations=evaluation->next;ui_complete_callback(evaluation->block,0,web_error("WKErrorDomain",2,"The page process exited.",web->url),1);_Block_release((void *)evaluation->block);free(evaluation);}Method method=web->delegate ? find_implementation(web->delegate,"webViewWebContentProcessDidTerminate:") : NULL;if(web->wk && method)method(web->delegate,"webViewWebContentProcessDidTerminate:",web->object,0,0,0);}
    }
    may_unwind=0;if(failed){jclass type=(*env)->GetObjectClass(env,activity);jmethodID method=(*env)->GetMethodID(env,type,"guestFailure","(Ljava/lang/String;)V");jstring error=(*env)->NewStringUTF(env,status);(*env)->CallVoidMethod(env,activity,method,error);(*env)->DeleteLocalRef(env,error);(*env)->DeleteLocalRef(env,type);return -1;}return (jlong)handled;
}
static Method web_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if(!receiver || class_receiver)return NULL;Word cls=read_word(object);int web=class_inherits(cls,external_class("_OBJC_CLASS_$_WKWebView")) || class_inherits(cls,external_class("_OBJC_CLASS_$_UIWebView"));
    if(web){if(!strcmp(selector,"initWithFrame:configuration:"))return (Method)web_init;if(!strcmp(selector,"initWithFrame:"))return (Method)web_init_frame;if(!strcmp(selector,"init"))return (Method)web_init_plain;if(!strcmp(selector,"loadRequest:"))return (Method)web_load_request;if(!strcmp(selector,"loadHTMLString:baseURL:"))return (Method)web_load_html;if(!strcmp(selector,"loadData:MIMEType:characterEncodingName:baseURL:") || !strcmp(selector,"loadData:MIMEType:textEncodingName:baseURL:"))return (Method)web_load_data;if(!strcmp(selector,"evaluateJavaScript:completionHandler:"))return (Method)web_evaluate;if(!strcmp(selector,"stopLoading"))return (Method)web_stop;if(!strcmp(selector,"reload"))return (Method)web_reload;if(!strcmp(selector,"goBack") || !strcmp(selector,"goForward"))return (Method)web_history;if(!strcmp(selector,"dealloc"))return (Method)web_dealloc;if(!strcmp(selector,"estimatedProgress"))return (Method)web_progress;
        const char *getters[]={"URL","title","request","configuration","navigationDelegate","delegate","UIDelegate","scrollView","customUserAgent","canGoBack","canGoForward","isLoading","loading","scalesPageToFit"};for(unsigned i=0;i<COUNT(getters);++i)if(!strcmp(selector,getters[i]))return (Method)web_property;const char *setters[]={"setNavigationDelegate:","setDelegate:","setUIDelegate:","setCustomUserAgent:","setScalesPageToFit:"};for(unsigned i=0;i<COUNT(setters);++i)if(!strcmp(selector,setters[i]))return (Method)web_set_property;}
    if(!strcmp(receiver,"_OBJC_CLASS_$_WKWebViewConfiguration")){if(!strcmp(selector,"init"))return (Method)web_model_init;if(!strcmp(selector,"copy") || !strcmp(selector,"copyWithZone:"))return (Method)web_configuration_copy;const char *getters[]={"preferences","userContentController","applicationNameForUserAgent","mediaTypesRequiringUserActionForPlayback","dataDetectorTypes"};for(unsigned i=0;i<COUNT(getters);++i)if(!strcmp(selector,getters[i]))return (Method)web_configuration_property;const char *setters[]={"setPreferences:","setUserContentController:","setApplicationNameForUserAgent:","setMediaTypesRequiringUserActionForPlayback:","setDataDetectorTypes:"};for(unsigned i=0;i<COUNT(setters);++i)if(!strcmp(selector,setters[i]))return (Method)web_configuration_set;}
    if(!strcmp(receiver,"_OBJC_CLASS_$_WKPreferences")){if(!strcmp(selector,"init"))return (Method)web_model_init;if(!strcmp(selector,"javaScriptEnabled") || !strcmp(selector,"javaScriptCanOpenWindowsAutomatically"))return (Method)web_preference_get;if(!strcmp(selector,"setJavaScriptEnabled:") || !strcmp(selector,"setJavaScriptCanOpenWindowsAutomatically:"))return (Method)web_preference_set;if(!strcmp(selector,"minimumFontSize"))return (Method)web_minimum_font_get;if(!strcmp(selector,"setMinimumFontSize:"))return (Method)web_minimum_font;}
    if(!strcmp(receiver,"_OBJC_CLASS_$_WKUserContentController")){if(!strcmp(selector,"init"))return (Method)web_model_init;if(!strcmp(selector,"addUserScript:"))return (Method)web_add_script;if(!strcmp(selector,"removeAllUserScripts"))return (Method)web_remove_scripts;if(!strcmp(selector,"userScripts"))return (Method)web_scripts;}
    if(!strcmp(receiver,"_OBJC_CLASS_$_WKUserScript")){if(!strcmp(selector,"initWithSource:injectionTime:forMainFrameOnly:"))return (Method)web_script_init;if(!strcmp(selector,"source") || !strcmp(selector,"injectionTime") || !strcmp(selector,"isForMainFrameOnly"))return (Method)web_record_property;}
    if((!strcmp(receiver,"_OBJC_CLASS_$_WKNavigationAction") && (!strcmp(selector,"request") || !strcmp(selector,"navigationType") || !strcmp(selector,"sourceFrame") || !strcmp(selector,"targetFrame"))) || (!strcmp(receiver,"_OBJC_CLASS_$_WKFrameInfo") && (!strcmp(selector,"request") || !strcmp(selector,"isMainFrame"))))return (Method)web_record_property;
    if(class_inherits(cls,external_class("_OBJC_CLASS_$_UIScrollView"))){if(!strcmp(selector,"contentOffset"))return (Method)scroll_offset;if(!strcmp(selector,"contentSize"))return (Method)scroll_size;if(!strcmp(selector,"contentInset"))return (Method)scroll_insets;if(!strcmp(selector,"setContentOffset:"))return (Method)scroll_set_offset;if(!strcmp(selector,"setContentSize:"))return (Method)scroll_set_size;if(!strcmp(selector,"setContentInset:"))return (Method)scroll_set_insets;const char *getters[]={"delegate","isScrollEnabled","showsVerticalScrollIndicator","showsHorizontalScrollIndicator","isDirectionalLockEnabled","scrollsToTop"};for(unsigned i=0;i<COUNT(getters);++i)if(!strcmp(selector,getters[i]))return (Method)scroll_property;const char *setters[]={"setDelegate:","setScrollEnabled:","setShowsVerticalScrollIndicator:","setShowsHorizontalScrollIndicator:","setDirectionalLockEnabled:","setScrollsToTop:"};for(unsigned i=0;i<COUNT(setters);++i)if(!strcmp(selector,setters[i]))return (Method)scroll_set_property;}
    return NULL;
}
static Method ui_platform_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver) return NULL;
    Method web=web_method(object,selector,receiver,class_receiver);if(web)return web;
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
