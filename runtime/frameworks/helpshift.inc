struct HelpshiftBridgeState { Word delegate,instance,metadata; unsigned unread; };
static struct HelpshiftBridgeState helpshift_state;
static pthread_mutex_t helpshift_mutex=PTHREAD_MUTEX_INITIALIZER;
static jmethodID helpshift_command_method;

static struct GuestMap *helpshift_payload(void) { return (void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary")); }
static void helpshift_field(struct GuestMap *payload,const char *key,Word value) {
    if (value) map_set(payload,"Helpshift",value,copy_string(key));
}
static Word helpshift_configuration(Word value) {
    if (!value) return (Word)helpshift_payload();
    const char *base=external_base(read_word(value));
    if (base && (!strcmp(base,"_OBJC_CLASS_$_NSDictionary") || !strcmp(base,"_OBJC_CLASS_$_NSMutableDictionary"))) return value;
    Method convert=lookup(read_word(value),"toDictionary");
    return convert ? convert(value,"toDictionary",0,0,0,0) : value;
}
static int helpshift_command(const char *operation,struct GuestMap *payload) {
    char *bytes=NULL; size_t size=0;
    FILE *stream=open_memstream(&bytes,&size);
    if (!stream) stop("Allocation failed","Helpshift command");
    Word ancestors[128]; int valid=json_emit(stream,(Word)payload,0,ancestors,0);
    if (fclose(stream)) valid=0;
    if (!valid) {
#if I2A_DEVELOPMENT
        for (size_t i=0;i<payload->count;++i) {
            const struct MapEntry *entry=payload->entries+i;
            LOG("Helpshift conversion failed: operation=%s field=%s class=%s",operation,
                string_bytes(entry->key,"Helpshift field"),class_name(read_word(entry->value)));
        }
#endif
        free(bytes); return 0;
    }
    JNIEnv *env=attached_environment("Helpshift");
    if (!helpshift_command_method) {
        jclass cls=(*env)->GetObjectClass(env,activity);
        helpshift_command_method=(*env)->GetMethodID(env,cls,"helpshiftCommand","(Ljava/lang/String;Ljava/lang/String;)V");
        (*env)->DeleteLocalRef(env,cls);
    }
    jstring action=(*env)->NewStringUTF(env,operation);
    Word text=string_from_utf8(external_class("_OBJC_CLASS_$_NSString"),"Helpshift JSON",bytes);
    jstring data=http_java_string(env,text);
    free(bytes);
    (*env)->CallVoidMethod(env,activity,helpshift_command_method,action,data);
    (*env)->DeleteLocalRef(env,action); (*env)->DeleteLocalRef(env,data);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); LOG("Android Helpshift command failed: %s",operation); return 0; }
    return 1;
}
static void helpshift_install(Word self,const char *selector,Word key,Word domain,Word identifier,Word options) {
    struct GuestMap *payload=helpshift_payload();
    helpshift_field(payload,"api_key",key); helpshift_field(payload,"domain",domain);
    helpshift_field(payload,"ios_app_id",identifier);
    helpshift_field(payload,"config",HELPSHIFT_NATIVE_BACKEND && strstr(selector,"with") ? helpshift_configuration(options) : 0);
    helpshift_command("install",payload);
}
static void helpshift_install_platform(Word self,const char *selector,Word identifier,Word domain,Word config) {
    struct GuestMap *payload=helpshift_payload();
    helpshift_field(payload,"ios_platform_id",identifier); helpshift_field(payload,"domain",domain);
    helpshift_field(payload,"config",HELPSHIFT_NATIVE_BACKEND ? helpshift_configuration(config) : 0); helpshift_command("install",payload);
}
static Word helpshift_singleton(Word cls) {
    pthread_mutex_lock(&helpshift_mutex);
    if (!helpshift_state.instance) {
        size_t size=instance_size(cls);
        void *instance=calloc(1,size<sizeof(Word) ? sizeof(Word) : size);
        if (!instance) { pthread_mutex_unlock(&helpshift_mutex); stop("Allocation failed","HelpshiftSupport"); }
        *(Word *)instance=cls; helpshift_state.instance=(Word)instance;
    }
    Word instance=helpshift_state.instance; pthread_mutex_unlock(&helpshift_mutex); return instance;
}
static void helpshift_delegate(Word self,const char *selector,Word delegate) {
    pthread_mutex_lock(&helpshift_mutex); helpshift_state.delegate=delegate; pthread_mutex_unlock(&helpshift_mutex);
}
static Word helpshift_get_delegate(void) {
    pthread_mutex_lock(&helpshift_mutex); Word delegate=helpshift_state.delegate; pthread_mutex_unlock(&helpshift_mutex); return delegate;
}
static void helpshift_metadata(Word self,const char *selector,Word block) {
    Word copied=block ? (Word)_Block_copy((void *)block) : 0;
    pthread_mutex_lock(&helpshift_mutex); Word previous=helpshift_state.metadata; helpshift_state.metadata=copied; pthread_mutex_unlock(&helpshift_mutex);
    if (previous) _Block_release((void *)previous);
}
static void helpshift_show(Word self,const char *selector,Word first,Word second,Word third) {
    int single=strstr(selector,"SingleFAQ")!=NULL,section=strstr(selector,"FAQSection")!=NULL;
    struct GuestMap *payload=helpshift_payload();
    Word config=HELPSHIFT_NATIVE_BACKEND ? helpshift_configuration(single || section ? third : second) : 0;
    helpshift_field(payload,"config",config);
    if (single || section) helpshift_field(payload,"publish_id",first);
    pthread_mutex_lock(&helpshift_mutex);
    Word block=HELPSHIFT_NATIVE_BACKEND && helpshift_state.metadata ? (Word)_Block_copy((void *)helpshift_state.metadata) : 0;
    pthread_mutex_unlock(&helpshift_mutex);
    if (block) {
        Word metadata=((Word (*)(void *))((struct Block_layout *)block)->invoke)((void *)block);
        helpshift_field(payload,"metadata",metadata); _Block_release((void *)block);
    }
    helpshift_command(single ? "faq" : section ? "section" : strstr(selector,"Conversation") ? "conversation" : "faqs",payload);
}
static void helpshift_login(Word self,const char *selector,Word identifier,Word name,Word email) {
    struct GuestMap *payload=helpshift_payload();
    helpshift_field(payload,"identifier",identifier); helpshift_field(payload,"name",name); helpshift_field(payload,"email",email);
    helpshift_command("login",payload);
}
static void helpshift_user_identifier(Word self,const char *selector,Word identifier) {
    struct GuestMap *payload=helpshift_payload(); helpshift_field(payload,"identifier",identifier); helpshift_command("user",payload);
}
static void helpshift_user_name(Word self,const char *selector,Word name,Word email) {
    struct GuestMap *payload=helpshift_payload(); helpshift_field(payload,"name",name); helpshift_field(payload,"email",email); helpshift_command("user",payload);
}
static void helpshift_logout(void) { helpshift_command("logout",helpshift_payload()); }
static Word helpshift_language(Word self,const char *selector,Word language) {
    struct GuestMap *payload=helpshift_payload(); helpshift_field(payload,"language",language); return helpshift_command("language",payload);
}
static Word helpshift_unread(Word self,const char *selector,Word remote) {
    struct GuestMap *payload=helpshift_payload(); helpshift_field(payload,"remote",number_bool(external_class("_OBJC_CLASS_$_NSNumber"),0,remote!=0));
    helpshift_command("unread",payload);
    pthread_mutex_lock(&helpshift_mutex); unsigned count=helpshift_state.unread; pthread_mutex_unlock(&helpshift_mutex); return count;
}
static void helpshift_breadcrumb(Word self,const char *selector,Word value) {
    struct GuestMap *payload=helpshift_payload(); helpshift_field(payload,"value",value); helpshift_command("breadcrumb",payload);
}
static void helpshift_clear_breadcrumbs(void) { helpshift_command("clear_breadcrumbs",helpshift_payload()); }
static void helpshift_pause_notifications(Word self,const char *selector,Word paused) {
    struct GuestMap *payload=helpshift_payload(); helpshift_field(payload,"paused",number_bool(external_class("_OBJC_CLASS_$_NSNumber"),0,paused!=0)); helpshift_command("pause_notifications",payload);
}
static Word helpshift_push(Word self,const char *selector,Word data) {
    struct GuestMap *payload=helpshift_payload(); helpshift_field(payload,"data",data); return helpshift_command("push",payload);
}
static void helpshift_push_token(Word self,const char *selector,Word token) {
    const char *base=token ? external_base(read_word(token)) : NULL;
    struct GuestMap *payload=helpshift_payload();
    if (base && (!strcmp(base,"_OBJC_CLASS_$_NSString") || !strcmp(base,"_OBJC_CLASS_$_NSMutableString"))) {
        helpshift_field(payload,"token",token); helpshift_command("push_token",payload);
    } else helpshift_command("ios_push_token",payload);
}

struct HelpshiftEventDelivery { char *name,*data; };
static void helpshift_deliver_event(void *context) {
    struct HelpshiftEventDelivery *event=context;
    const char *selector=NULL; Word argument=0;
    if (!strcmp(event->name,"session_started")) selector="helpshiftSessionHasBegun";
    else if (!strcmp(event->name,"session_ended")) selector="helpshiftSessionHasEnded";
    else if (!strcmp(event->name,"unread")) {
        plist_t data=NULL;
        if (plist_from_json(event->data,(uint32_t)strlen(event->data),&data)==PLIST_ERR_SUCCESS) {
            plist_t count=plist_dict_get_item(data,"count"); uint64_t value=0;
            if (count && plist_get_node_type(count)==PLIST_UINT) plist_get_uint_val(count,&value);
            pthread_mutex_lock(&helpshift_mutex); helpshift_state.unread=value>UINT_MAX ? UINT_MAX : (unsigned)value; argument=helpshift_state.unread; pthread_mutex_unlock(&helpshift_mutex);
            plist_free(data);
        }
        selector="didReceiveNotificationCount:";
    }
    free(event->name); free(event->data); free(event);
    Word delegate=helpshift_get_delegate();
    if (selector && delegate) {
        Method callback=find_implementation(delegate,selector);
        if (callback) callback(delegate,selector,argument,0,0,0);
    }
}
JNIEXPORT void JNICALL HOST_JNI(HostActivity_deliverHelpshiftEvent)(JNIEnv *env,jobject instance,jstring name,jstring data) {
    const char *event=(*env)->GetStringUTFChars(env,name,NULL),*payload=(*env)->GetStringUTFChars(env,data,NULL);
    if (!event || !payload) {
        if (event) (*env)->ReleaseStringUTFChars(env,name,event);
        if (payload) (*env)->ReleaseStringUTFChars(env,data,payload);
        return;
    }
    struct HelpshiftEventDelivery *delivery=calloc(1,sizeof(*delivery));
    if (delivery) { delivery->name=strdup(event); delivery->data=strdup(payload); }
    (*env)->ReleaseStringUTFChars(env,name,event); (*env)->ReleaseStringUTFChars(env,data,payload);
    if (!delivery || !delivery->name || !delivery->data) {
        if (delivery) { free(delivery->name); free(delivery->data); free(delivery); } return;
    }
    schedule_main_function(helpshift_deliver_event,delivery);
}
static void initialize_helpshift_bridge(void) {
    static const struct { const char *selector; Method implementation; } methods[]={
        {"installForApiKey:domainName:appID:",(Method)helpshift_install},
        {"installForApiKey:domainName:appID:withOptions:",(Method)helpshift_install},
        {"installForApiKey:domainName:appID:withConfig:",(Method)helpshift_install},
        {"_installForApiKey:domainName:appID:",(Method)helpshift_install},
        {"_installForApiKey:domainName:appID:withOptions:",(Method)helpshift_install},
        {"installWithPlatformId:domain:config:",(Method)helpshift_install_platform},
        {"sharedInstance",(Method)helpshift_singleton},
        {"delegate",(Method)helpshift_get_delegate},{"setDelegate:",(Method)helpshift_delegate},
        {"setHelpshiftDelegate:",(Method)helpshift_delegate},
        {"showConversation:withOptions:",(Method)helpshift_show},{"showConversation:withConfig:",(Method)helpshift_show},
        {"showFAQs:withOptions:",(Method)helpshift_show},{"showFAQs:withConfig:",(Method)helpshift_show},
        {"showFAQSection:withController:withOptions:",(Method)helpshift_show},{"showFAQSection:withController:withConfig:",(Method)helpshift_show},
        {"showSingleFAQ:withController:withOptions:",(Method)helpshift_show},{"showSingleFAQ:withController:withConfig:",(Method)helpshift_show},
        {"loginWithIdentifier:withName:andEmail:",(Method)helpshift_login},{"logout",(Method)helpshift_logout},
        {"setName:andEmail:",(Method)helpshift_user_name},{"setUserIdentifier:",(Method)helpshift_user_identifier},
        {"setLanguage:",(Method)helpshift_language},{"setSDKLanguage:",(Method)helpshift_language},
        {"setMetadataBlock:",(Method)helpshift_metadata},{"setMetadataObjectBlock:",(Method)helpshift_metadata},
        {"getNotificationCountFromRemote:",(Method)helpshift_unread},{"leaveBreadCrumb:",(Method)helpshift_breadcrumb},
        {"clearBreadCrumbs",(Method)helpshift_clear_breadcrumbs},
        {"pauseDisplayOfInAppNotification:",(Method)helpshift_pause_notifications},
        {"handleRemoteNotification:withController:",(Method)helpshift_push},
        {"registerDeviceToken:",(Method)helpshift_push_token}
    };
    const Word *classes=(void *)(aslr+CLASSES_OFFSET); unsigned registered=0;
    for (unsigned i=0;i<CLASSES_COUNT;++i) {
        Word cls=classes[i]; const char *name=class_name(cls);
        if (!name || (strcmp(name,"HelpshiftCore") && strcmp(name,"HelpshiftSupport") && strcmp(name,"Helpshift"))) continue;
        for (unsigned scope=0;scope<2;++scope) {
            Word target=scope ? read_word(cls) : cls,ro=read_word(target+32)&~(Word)7;
            Word list=read_word(ro+32);
            for (unsigned j=0;j<COUNT(methods);++j) {
                Word record=find_method_record(list,methods[j].selector);
                if (record) { __atomic_store_n((Method *)(record+16),methods[j].implementation,__ATOMIC_RELEASE); ++registered; }
            }
        }
    }
    if (registered) { __atomic_add_fetch(&method_cache_epoch,1,__ATOMIC_RELEASE); LOG("Helpshift public bridge: %u methods registered",registered); }
}
