/* Queued notifications coalesce on the original name/sender and are delivered
 * by the calling thread's runloop on its next turn (NSPostASAP). */
struct PendingNotice { Word notification; struct PendingNotice *next; };
struct NotificationQueue { Word isa,center; struct PendingNotice *first,*last; int scheduled; };
static _Thread_local Word default_notification_queue;
static pthread_mutex_t queued_notices_mutex=PTHREAD_MUTEX_INITIALIZER;
static Word notification_queue_create(Word cls,Word center) {
    struct NotificationQueue *queue=calloc(1,sizeof(*queue));
    if (!queue) stop("Allocation failed","NSNotificationQueue");
    queue->isa=cls; queue->center=center ? center : singleton_center(external_class("_OBJC_CLASS_$_NSNotificationCenter"));
    return (Word)queue;
}
static Word notification_queue_default(Word cls) {
    if (!default_notification_queue) default_notification_queue=notification_queue_create(cls,0);
    return default_notification_queue;
}
static Word notification_queue_init(Word object,const char *selector,Word center) {
    Word result=notification_queue_create(read_word(object),center); free((void *)object); return result;
}
static Word notification_queue_init_default(Word object,const char *selector) { return notification_queue_init(object,selector,0); }
static int queued_notice_matches(Word first,Word second,Word mask) {
    struct HostObject *a=(void *)first,*b=(void *)second;
    return mask && (!(mask&1) || same_objects(a->content[0],b->content[0])) && (!(mask&2) || a->content[1]==b->content[1]);
}
static void notification_queue_drain(struct NotificationQueue *queue,const char *selector) {
    pthread_mutex_lock(&queued_notices_mutex);
    struct PendingNotice *list=queue->first; queue->first=queue->last=NULL; queue->scheduled=0;
    pthread_mutex_unlock(&queued_notices_mutex);
    while (list) { struct PendingNotice *next=list->next; notification_post(queue->center,selector,(void *)list->notification); free(list); list=next; }
}
static void notification_queue_enqueue(struct NotificationQueue *queue,const char *selector,Word notification,Word style,Word mask,Word modes) {
    if (!notification || style<1 || style>3 || mask>3) stop("Invalid notification queue arguments",selector);
    scheduling_modes(modes,selector);
    if (style==3) { notification_post(queue->center,selector,(void *)notification); return; }
    if (style==1) stop("Idle notification delivery is not implemented",selector);
    pthread_mutex_lock(&queued_notices_mutex);
    for (struct PendingNotice *item=queue->first;item;item=item->next)
        if (queued_notice_matches(item->notification,notification,mask)) { pthread_mutex_unlock(&queued_notices_mutex); return; }
    struct PendingNotice *item=calloc(1,sizeof(*item));
    if (!item) { pthread_mutex_unlock(&queued_notices_mutex); stop("Allocation failed",selector); }
    item->notification=notification;
    if (queue->last) queue->last->next=item; else queue->first=item;
    queue->last=item; int schedule=!queue->scheduled; queue->scheduled=1;
    pthread_mutex_unlock(&queued_notices_mutex);
    if (schedule) perform_enqueue((Word)queue,"i2aDrainQueuedNotifications",0,core_current_runloop(),0);
}
static void notification_queue_enqueue_basic(struct NotificationQueue *queue,const char *selector,Word notification,Word style) { notification_queue_enqueue(queue,selector,notification,style,3,0); }
static void notification_queue_remove(struct NotificationQueue *queue,const char *selector,Word notification,Word mask) {
    pthread_mutex_lock(&queued_notices_mutex);
    struct PendingNotice **slot=&queue->first; queue->last=NULL;
    while (*slot) {
        struct PendingNotice *item=*slot;
        if (queued_notice_matches(item->notification,notification,mask)) { *slot=item->next; free(item); }
        else { queue->last=item; slot=&item->next; }
    }
    pthread_mutex_unlock(&queued_notices_mutex);
}
static Method notification_queue_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver || strcmp(receiver,"_OBJC_CLASS_$_NSNotificationQueue")) return NULL;
    if (class_receiver && !strcmp(selector,"defaultQueue")) return (Method)notification_queue_default;
    if (!class_receiver) {
        if (!strcmp(selector,"init")) return (Method)notification_queue_init_default;
        if (!strcmp(selector,"initWithNotificationCenter:")) return (Method)notification_queue_init;
        if (!strcmp(selector,"enqueueNotification:postingStyle:")) return (Method)notification_queue_enqueue_basic;
        if (!strcmp(selector,"enqueueNotification:postingStyle:coalesceMask:forModes:")) return (Method)notification_queue_enqueue;
        if (!strcmp(selector,"dequeueNotificationsMatching:coalesceMask:")) return (Method)notification_queue_remove;
        if (!strcmp(selector,"i2aDrainQueuedNotifications")) return (Method)notification_queue_drain;
    }
    return NULL;
}
