 
static struct HostObject local_cf_center;
struct Notice { struct Observer *observer; Word receiver,selector,queue,block,callback; };
struct NoticeBlock { struct Block_layout layout; struct Notice *notice; Word notification; };
static Word notification_create(Word cls,const char *selector,Word name,Word object,Word info) {
    if (!name) stop("Nil notification name",selector);
    struct HostObject *notice=(void *)host_allocate(cls);
    notice->content[0]=name; notice->content[1]=object; notice->content[2]=info;
    return (Word)notice;
}
static Word notification_basic(Word cls,const char *selector,Word name,Word object) { return notification_create(cls,selector,name,object,0); }
static Word notification_init(Word object,const char *selector,Word name,Word source,Word info) {
    Word result=notification_create(read_word(object),selector,name,source,info); free((void *)object); return result;
}
static Word notification_name(struct HostObject *notice) { return notice->content[0]; }
static Word notification_source(struct HostObject *notice) { return notice->content[1]; }
static Word notification_info(struct HostObject *notice) { return notice->content[2]; }
static void notice_deliver(struct Notice *delivery,Word object) {
    if (!__atomic_load_n(&delivery->observer->active,__ATOMIC_ACQUIRE)) return;
    if (delivery->callback) {
        struct HostObject *notice=(void *)object;
        ((void (*)(Word,Word,Word,Word,Word))delivery->callback)(delivery->observer->center,delivery->receiver,notice->content[0],notice->content[1],notice->content[2]);
    } else if (delivery->block) ((void (*)(Word,Word))read_word(delivery->block+16))(delivery->block,object);
    else {
        const char *selector=(void *)delivery->selector;
        Method method=find_implementation(delivery->receiver,selector);
        if (!method) stop("Missing notification observer method",selector);
        method(delivery->receiver,selector,object,0,0,0);
    }
}
static void notice_block_invoke(struct NoticeBlock *block) { notice_deliver(block->notice,block->notification); }
static void notification_post(Word center,const char *selector,struct HostObject *notification) {
    if (!notification) stop("Nil notification",selector);
    Word name=notification->content[0],source=notification->content[1];
    pthread_mutex_lock(&observers_mutex);
    size_t count=0;
    for (struct Observer *item=observers;item;item=item->next)
        if (item->active && item->center==center && (!item->name || same_objects(name,item->name)) && (!item->object || item->object==source)) ++count;
    struct Notice *snapshot=calloc(count,sizeof(*snapshot));
    if (count && !snapshot) { pthread_mutex_unlock(&observers_mutex); stop("Allocation failed",selector); }
    size_t index=0;
    for (struct Observer *item=observers;item;item=item->next) {
        if (!item->active || item->center!=center || (item->name && !same_objects(name,item->name)) || (item->object && item->object!=source)) continue;
        snapshot[index++]=(struct Notice){item,item->receiver,item->selector,item->queue,item->block ? (Word)_Block_copy((void *)item->block) : 0,item->callback};
    }
    pthread_mutex_unlock(&observers_mutex);
    for (size_t i=0;i<count;++i) {
        if (snapshot[i].queue && snapshot[i].queue!=current_operation_queue(external_class("_OBJC_CLASS_$_NSOperationQueue"))) {
            static struct Block_descriptor_1 descriptor={0,sizeof(struct NoticeBlock)};
            struct NoticeBlock block={.layout={.isa=_NSConcreteStackBlock,.flags=0,.reserved=0,.invoke=(void (*)(void *,...))notice_block_invoke,.descriptor=&descriptor},.notice=snapshot+i,.notification=(Word)notification};
            Word operation=block_operation(external_class("_OBJC_CLASS_$_NSBlockOperation"),selector,(Word)&block);
            queue_add(snapshot[i].queue,selector,operation);
            operation_wait(operation,selector);
        } else notice_deliver(snapshot+i,(Word)notification);
        if (snapshot[i].block) _Block_release((void *)snapshot[i].block);
    }
    free(snapshot);
}
static void notification_post_name(Word center,const char *selector,Word name,Word object,Word info) {
    Word notice=notification_create(external_class("_OBJC_CLASS_$_NSNotification"),selector,name,object,info);
    notification_post(center,selector,(void *)notice);
}
static void notification_post_basic(Word center,const char *selector,Word name,Word object) { notification_post_name(center,selector,name,object,0); }
static void notification_remove_filtered(Word center,const char *selector,Word receiver,Word name,Word object) {
    pthread_mutex_lock(&observers_mutex);
    for (struct Observer *item=observers;item;item=item->next) {
        if (item->center!=center || (item->receiver!=receiver && (Word)item!=receiver) || (name && !same_objects(item->name,name)) || (object && item->object!=object)) continue;
        __atomic_store_n(&item->active,0,__ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&observers_mutex);
}
static void notification_remove(Word center,const char *selector,Word receiver) { notification_remove_filtered(center,selector,receiver,0,0); }
static Word cf_local_center(void) { local_cf_center.isa=external_class("_OBJC_CLASS_$_NSObject"); return (Word)&local_cf_center; }
static void cf_notification_add(Word center,Word observer,Word callback,Word name,Word object,long suspension) {
    if (center!=cf_local_center() || !callback) stop("Invalid local notification center","CFNotificationCenterAddObserver");
    make_observer(center,observer,0,name,object,0,0,callback);
}
static void cf_notification_remove(Word center,Word observer,Word name,Word object) { notification_remove_filtered(center,"CFNotificationCenterRemoveObserver",observer,name,object); }
static void cf_notification_remove_all(Word center,Word observer) { notification_remove(center,"CFNotificationCenterRemoveEveryObserver",observer); }
static void cf_notification_post(Word center,Word name,Word object,Word info,Word options) { notification_post_name(center,"CFNotificationCenterPostNotification",name,object,info); }
static void platform_notification(const char *name,Word object,Word info) {
    Word string=copy_string(name);
    notification_post_name(singleton_center(external_class("_OBJC_CLASS_$_NSNotificationCenter")),"Android notification",string,object,info);
    cf_notification_post(cf_local_center(),string,object,info,0);
}
static Word resolve_notification_import(const char *name) {
    if (!strcmp(name,"_CFNotificationCenterGetLocalCenter")) return (Word)cf_local_center;
    if (!strcmp(name,"_CFNotificationCenterAddObserver")) return (Word)cf_notification_add;
    if (!strcmp(name,"_CFNotificationCenterRemoveObserver")) return (Word)cf_notification_remove;
    if (!strcmp(name,"_CFNotificationCenterRemoveEveryObserver")) return (Word)cf_notification_remove_all;
    if (!strcmp(name,"_CFNotificationCenterPostNotification") || !strcmp(name,"_CFNotificationCenterPostNotificationWithOptions")) return (Word)cf_notification_post;
    return 0;
}
static Method notifications_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver) return NULL;
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSNotification")) {
        if (class_receiver && !strcmp(selector,"notificationWithName:object:userInfo:")) return (Method)notification_create;
        if (class_receiver && !strcmp(selector,"notificationWithName:object:")) return (Method)notification_basic;
        if (!class_receiver) {
            if (!strcmp(selector,"initWithName:object:userInfo:")) return (Method)notification_init;
            if (!strcmp(selector,"name")) return (Method)notification_name;
            if (!strcmp(selector,"object")) return (Method)notification_source;
            if (!strcmp(selector,"userInfo")) return (Method)notification_info;
            if (!strcmp(selector,"copy")) return (Method)identity;
        }
    }
    if (!class_receiver && !strcmp(receiver,"_OBJC_CLASS_$_NSNotificationCenter")) {
        if (!strcmp(selector,"postNotification:")) return (Method)notification_post;
        if (!strcmp(selector,"postNotificationName:object:")) return (Method)notification_post_basic;
        if (!strcmp(selector,"postNotificationName:object:userInfo:")) return (Method)notification_post_name;
        if (!strcmp(selector,"removeObserver:")) return (Method)notification_remove;
        if (!strcmp(selector,"removeObserver:name:object:")) return (Method)notification_remove_filtered;
    }
    return NULL;
}
