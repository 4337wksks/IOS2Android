 
struct GuestRect { double x,y,width,height; };
static Word value_rect(Word cls,const char *selector,struct GuestRect rect) {
    struct BoxedValue *value=calloc(1,sizeof(*value));
    if (!value) stop("Allocation failed",selector);
    value->isa=cls; value->type="{CGRect={CGPoint=dd}{CGSize=dd}}"; value->size=sizeof(rect);
    memcpy(value->bytes,&rect,sizeof(rect)); return (Word)value;
}
static struct GuestRect value_rect_get(struct BoxedValue *value,const char *selector) {
    if (value->size!=sizeof(struct GuestRect) || strcmp(value->type,"{CGRect={CGPoint=dd}{CGSize=dd}}"))
        stop("NSValue does not contain CGRect",selector);
    struct GuestRect rect; memcpy(&rect,value->bytes,sizeof(rect)); return rect;
}
struct DisplayState { int width,height,rotation,tablet,battery_state,low_power; double scale; float battery_level; };
static struct DisplayState display_state;
static struct DisplayState display_snapshot(void);
static pthread_mutex_t display_mutex=PTHREAD_MUTEX_INITIALIZER;
static struct HostObject screen_object;
static int battery_monitoring,orientation_monitoring;
static Word current_user_notification_settings;
static int current_application_state;
static int application_lifecycle_ready;
static long application_badge;
 
static long application_status_style;
static int application_status_hidden=1;
static double application_status_height;
JNIEXPORT void JNICALL HOST_JNI(HostActivity_updateStatusBarSnapshot)(JNIEnv *env,jobject instance,jlong style,jboolean hidden,jdouble height) {
    __atomic_store_n(&application_status_style,(long)style,__ATOMIC_RELAXED);
    __atomic_store_n(&application_status_hidden,hidden!=0,__ATOMIC_RELAXED);
    __atomic_store(&application_status_height,&height,__ATOMIC_RELAXED);
}
static Word application_status_bar_style(void) { return (Word)__atomic_load_n(&application_status_style,__ATOMIC_RELAXED); }
static Word application_status_bar_hidden(void) { return (Word)__atomic_load_n(&application_status_hidden,__ATOMIC_RELAXED); }
static struct GuestRect application_status_bar_frame(void) {
    struct DisplayState display=display_snapshot(); double height;
    __atomic_load(&application_status_height,&height,__ATOMIC_RELAXED);
    return (struct GuestRect){0,0,display.width/display.scale,height};
}
static void application_set_status_style(Word object,const char *selector,long style,Word animated) {
    if (style<0 || style>3) stop("Invalid status bar style",selector);
    if (__atomic_exchange_n(&application_status_style,style,__ATOMIC_RELAXED)==style) return;
    JNIEnv *env=attached_environment(selector); jclass cls=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,cls,"guestStatusBarStyle","(J)V");
    (*env)->CallVoidMethod(env,activity,method,(jlong)style); (*env)->DeleteLocalRef(env,cls);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); stop("Android status bar style failed",selector); }
}
static void application_set_status_style_plain(Word object,const char *selector,long style) { application_set_status_style(object,selector,style,0); }
static void application_set_status_hidden(Word object,const char *selector,unsigned char hidden,Word animation) {
    if (__atomic_exchange_n(&application_status_hidden,hidden!=0,__ATOMIC_RELAXED)==(hidden!=0)) return;
    JNIEnv *env=attached_environment(selector); jclass cls=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,cls,"guestStatusBarHidden","(Z)V");
    (*env)->CallVoidMethod(env,activity,method,(jboolean)(hidden!=0)); (*env)->DeleteLocalRef(env,cls);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); stop("Android status bar visibility failed",selector); }
}
static void application_set_status_hidden_plain(Word object,const char *selector,unsigned char hidden) { application_set_status_hidden(object,selector,hidden,0); }
static Word application_get_badge(void) { return (Word)application_badge; }
static void application_set_badge(Word object,const char *selector,long badge) {
    if (badge<0) badge=0;
    if (badge>INT32_MAX) badge=INT32_MAX;
    JNIEnv *env=attached_environment(selector); jclass cls=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,cls,"updateBadge","(I)V");
    (*env)->CallVoidMethod(env,activity,method,(jint)badge); (*env)->DeleteLocalRef(env,cls);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env); stop("Android badge update failed",selector); }
    application_badge=badge;
}
struct BackgroundTask { Word identifier,block; struct BackgroundTask *next; };
static struct BackgroundTask *background_tasks;
static Word next_background_task=1;
static pthread_mutex_t background_task_mutex=PTHREAD_MUTEX_INITIALIZER;
static void deliver_display_notifications(void *context) {
    unsigned changes=(unsigned)(Word)context;
    if (changes&1) platform_notification("UIDeviceBatteryLevelDidChangeNotification",(Word)&device,0);
    if (changes&2) platform_notification("UIDeviceBatteryStateDidChangeNotification",(Word)&device,0);
    if (changes&4) platform_notification("UIDeviceOrientationDidChangeNotification",(Word)&device,0);
    if (changes&8) platform_notification("NSProcessInfoPowerStateDidChangeNotification",(Word)&process_info,0);
}

JNIEXPORT void JNICALL HOST_JNI(HostActivity_updateSystemSnapshot)(JNIEnv *env,jobject activity,jint width,jint height,jfloat scale,jint rotation,jboolean tablet,jfloat battery,jint battery_state,jboolean low_power) {
    pthread_mutex_lock(&display_mutex);
    struct DisplayState previous=display_state;
    display_state=(struct DisplayState){width,height,rotation,tablet,battery_state,low_power,scale,battery};
    pthread_mutex_unlock(&display_mutex);
    if (java_vm && started) {
        unsigned changes=(battery_monitoring && previous.battery_level!=battery ? 1U : 0U) |
            (battery_monitoring && previous.battery_state!=battery_state ? 2U : 0U) |
            (orientation_monitoring && previous.rotation!=rotation ? 4U : 0U) | (previous.low_power!=low_power ? 8U : 0U);
        if (changes) schedule_main_function(deliver_display_notifications,(void *)(Word)changes);
    }
}
static struct DisplayState display_snapshot(void) {
    pthread_mutex_lock(&display_mutex);
    struct DisplayState result=display_state;
    pthread_mutex_unlock(&display_mutex);
    if (result.width<1 || result.height<1 || result.scale<=0) stop("Display snapshot unavailable","UIKit");
    return result;
}
static Word device_orientation(void) {
    struct DisplayState display=display_snapshot();
    if (display.width>display.height) return display.rotation==3 ? 4 : 3;
    return display.rotation==2 ? 2 : 1;
}
static Word interface_orientation(void) {
    Word orientation=device_orientation();
    return orientation==3 ? 4 : orientation==4 ? 3 : orientation;
}
static Word device_idiom(void) { return display_snapshot().tablet ? 1 : 0; }
static Word device_multitasking(void) { return 1; }
static void device_battery_monitor(Word self,const char *selector,unsigned char enabled) { battery_monitoring=enabled!=0; }
static Word device_battery_monitored(void) { return battery_monitoring; }
static float device_battery_level(void) { return battery_monitoring ? display_snapshot().battery_level : -1; }
static Word device_battery_state(void) { return battery_monitoring ? (Word)display_snapshot().battery_state : 0; }
static Word low_power_mode(void) { return display_snapshot().low_power; }
static void orientation_begin(void) {
    if (__atomic_fetch_add(&orientation_monitoring,1,__ATOMIC_RELAXED)==0)
        schedule_main_function(deliver_display_notifications,(void *)(Word)4);
}
static void orientation_end(void) {
    int value=__atomic_load_n(&orientation_monitoring,__ATOMIC_RELAXED);
    while (value>0 && !__atomic_compare_exchange_n(&orientation_monitoring,&value,value-1,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED)) {}
}
static Word orientation_generating(void) { return __atomic_load_n(&orientation_monitoring,__ATOMIC_RELAXED)>0; }
static Word main_screen(Word cls) { screen_object.isa=cls; return (Word)&screen_object; }
static struct GuestRect screen_bounds(void) { struct DisplayState s=display_snapshot(); return (struct GuestRect){0,0,s.width/s.scale,s.height/s.scale}; }
static struct GuestRect screen_native_bounds(void) { struct DisplayState s=display_snapshot(); return (struct GuestRect){0,0,s.width,s.height}; }
static double screen_scale(void) { return display_snapshot().scale; }
static Word notification_settings(Word cls,const char *selector,Word types,Word categories) {
    struct HostObject *settings=(void *)host_allocate(cls);
    settings->content[0]=types; settings->content[1]=categories;
    return (Word)settings;
}
static Word notification_types(struct HostObject *settings) { return settings->content[0]; }
static Word notification_categories(struct HostObject *settings) { return settings->content[1]; }
static void did_register_user_notifications(void *context) {
    Method method=find_implementation(app_delegate,"application:didRegisterUserNotificationSettings:");
    if (method) method(app_delegate,"application:didRegisterUserNotificationSettings:",(Word)&application,(Word)context,0,0);
}
static void register_user_notifications(Word self,const char *selector,struct HostObject *requested) {
    JNIEnv *env=attached_environment(selector);
    jclass cls=(*env)->GetObjectClass(env,activity);
    jmethodID enabled=(*env)->GetMethodID(env,cls,"notificationsEnabled","()Z");
    int allowed=(*env)->CallBooleanMethod(env,activity,enabled);
    (*env)->DeleteLocalRef(env,cls);
    current_user_notification_settings=notification_settings(requested->isa,selector,allowed ? requested->content[0] : 0,requested->content[1]);
    schedule_main_function(did_register_user_notifications,(void *)current_user_notification_settings);
}
static Word get_user_notification_settings(void) {
    if (!current_user_notification_settings) current_user_notification_settings=notification_settings(external_class("_OBJC_CLASS_$_UIUserNotificationSettings"),"currentUserNotificationSettings",0,0);
    return current_user_notification_settings;
}
static void remote_registration_failed(void *context) {
    Method method=find_implementation(app_delegate,"application:didFailToRegisterForRemoteNotificationsWithError:");
    if (method) method(app_delegate,"application:didFailToRegisterForRemoteNotificationsWithError:",(Word)&application,(Word)context,0,0);
}
static void register_remote_notifications(void) {
    struct GuestMap *info=(void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));
    map_set(info,"APNs",copy_string("APNs registration is unavailable on Android"),copy_string("NSLocalizedDescription"));
    Word error=error_with_domain(external_class("_OBJC_CLASS_$_NSError"),"APNs",copy_string("I2ACompatibilityErrorDomain"),1,(Word)info);
    schedule_main_function(remote_registration_failed,(void *)error);
}
static Word remote_notifications_registered(void) { return 0; }
static void cancel_local_notifications(void) {
    JNIEnv *env=attached_environment("cancelAllLocalNotifications");
    jclass cls=(*env)->GetObjectClass(env,activity);
    jmethodID cancel=(*env)->GetMethodID(env,cls,"cancelLocalNotifications","()V");
    (*env)->CallVoidMethod(env,activity,cancel);
    (*env)->DeleteLocalRef(env,cls);
}
static Word application_state(void) { return (Word)__atomic_load_n(&current_application_state,__ATOMIC_ACQUIRE); }
static void application_set_delegate(Word object,const char *selector,Word delegate) {
    Word key=copy_string("delegate"); key_will_change(object,selector,key); app_delegate=delegate; key_did_change(object,selector,key);
}
static struct HostObject user_notification_center;
static pthread_once_t user_notification_once=PTHREAD_ONCE_INIT;
static void user_notification_init(void) { user_notification_center.isa=external_class("_OBJC_CLASS_$_UNUserNotificationCenter"); }
static Word current_user_notification_center(void) {
    pthread_once(&user_notification_once,user_notification_init); return (Word)&user_notification_center;
}
static void user_notification_set_delegate(Word object,const char *selector,Word delegate) {
    __atomic_store_n(&((struct HostObject *)object)->content[0],delegate,__ATOMIC_RELEASE);
}
static Word user_notification_get_delegate(Word object,const char *selector) {
    return __atomic_load_n(&((struct HostObject *)object)->content[0],__ATOMIC_ACQUIRE);
}
static Word application_protected_data(void) {
    JNIEnv *env=attached_environment("isProtectedDataAvailable"); jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,type,"keychainUnlocked","()Z");
    Word result=(*env)->CallBooleanMethod(env,activity,method); (*env)->DeleteLocalRef(env,type); return result;
}
static void application_lifecycle_event(const char *selector,const char *notification) {
    Method callback=find_implementation(app_delegate,selector);
    if (callback) callback(app_delegate,selector,(Word)&application,0,0,0);
    platform_notification(notification,(Word)&application,0);
#if I2A_DEVELOPMENT
    LOG("[DIAG-lifecycle] %s state=%d tid=%d",selector,__atomic_load_n(&current_application_state,__ATOMIC_ACQUIRE),gettid());
#endif
}
static void application_lifecycle_transition(int value) {
    int previous=__atomic_load_n(&current_application_state,__ATOMIC_ACQUIRE);
    if (!application_lifecycle_ready || previous==value) {
        __atomic_store_n(&current_application_state,value,__ATOMIC_RELEASE); return;
    }
    if (previous==0) {
        __atomic_store_n(&current_application_state,1,__ATOMIC_RELEASE);
        application_lifecycle_event("applicationWillResignActive:","UIApplicationWillResignActiveNotification");
    } else if (previous==2) {
        __atomic_store_n(&current_application_state,1,__ATOMIC_RELEASE);
        application_lifecycle_event("applicationWillEnterForeground:","UIApplicationWillEnterForegroundNotification");
    }
    __atomic_store_n(&current_application_state,value,__ATOMIC_RELEASE);
    if (value==0) application_lifecycle_event("applicationDidBecomeActive:","UIApplicationDidBecomeActiveNotification");
    else if (value==2) application_lifecycle_event("applicationDidEnterBackground:","UIApplicationDidEnterBackgroundNotification");
}
static void application_lifecycle_start(void) {
    if (application_lifecycle_ready) return;
    int desired=__atomic_exchange_n(&current_application_state,1,__ATOMIC_ACQ_REL);
    application_lifecycle_ready=1;
    application_lifecycle_transition(desired);
}
JNIEXPORT jstring JNICALL HOST_JNI(HostActivity_updateApplicationState)(JNIEnv *env,jobject instance,jint value) {
    if (value<0 || value>2) return (*env)->NewStringUTF(env,"Invalid application state");
    may_unwind=1;
    snprintf(stage,sizeof(stage),"Android application lifecycle state=%d",value);
    int failed=setjmp(boundary);
    if (!failed) application_lifecycle_transition(value);
    may_unwind=0;
    return failed ? (*env)->NewStringUTF(env,status) : NULL;
}
static void end_background_task(Word self,const char *selector,Word identifier) {
    pthread_mutex_lock(&background_task_mutex);
    struct BackgroundTask **slot=&background_tasks;
    while (*slot && (*slot)->identifier!=identifier) slot=&(*slot)->next;
    struct BackgroundTask *task=*slot;
    if (task) *slot=task->next;
    pthread_mutex_unlock(&background_task_mutex);
    if (!task) return;
    JNIEnv *env=attached_environment(selector);
    jclass cls=(*env)->GetObjectClass(env,activity);
    jmethodID finish=(*env)->GetMethodID(env,cls,"endBackgroundTask","(J)V");
    (*env)->CallVoidMethod(env,activity,finish,(jlong)identifier);
    (*env)->DeleteLocalRef(env,cls);
    if (task->block) _Block_release((void *)task->block);
    free(task);
}
static Word begin_background_task(Word self,const char *selector,Word block) {
    struct BackgroundTask *task=calloc(1,sizeof(*task));
    if (!task) return UINT64_MAX;
    task->block=block ? (Word)_Block_copy((void *)block) : 0;
    pthread_mutex_lock(&background_task_mutex);
    task->identifier=next_background_task++; task->next=background_tasks; background_tasks=task;
    Word identifier=task->identifier;
    pthread_mutex_unlock(&background_task_mutex);
    JNIEnv *env=attached_environment(selector);
    jclass cls=(*env)->GetObjectClass(env,activity);
    jmethodID begin=(*env)->GetMethodID(env,cls,"beginBackgroundTask","(J)Z");
    jboolean accepted=(*env)->CallBooleanMethod(env,activity,begin,(jlong)identifier);
    (*env)->DeleteLocalRef(env,cls);
    if (!accepted) { end_background_task(self,selector,identifier); return UINT64_MAX; }
    return identifier;
}
static Word begin_named_background_task(Word self,const char *selector,Word name,Word block) { return begin_background_task(self,selector,block); }
JNIEXPORT jstring JNICALL HOST_JNI(HostActivity_expireBackgroundTask)(JNIEnv *env,jobject instance,jlong identifier) {
    pthread_mutex_lock(&background_task_mutex);
    struct BackgroundTask *task=background_tasks;
    while (task && task->identifier!=(Word)identifier) task=task->next;
    Word block=task && task->block ? (Word)_Block_copy((void *)task->block) : 0;
    pthread_mutex_unlock(&background_task_mutex);
    int failed=0;
    if (block) {
        may_unwind=1;
        snprintf(stage,sizeof(stage),"Background task expiration");
        failed=setjmp(boundary);
        if (!failed) ((void (*)(Word))read_word(block+16))(block);
        may_unwind=0;
        _Block_release((void *)block);
    }
    end_background_task((Word)&application,"endBackgroundTask:",(Word)identifier);
    return failed ? (*env)->NewStringUTF(env,status) : NULL;
}
static unsigned accessibility_state;
JNIEXPORT void JNICALL HOST_JNI(HostActivity_updateGuestAccessibility)(JNIEnv *env,jobject instance,jboolean spoken) { __atomic_store_n(&accessibility_state,spoken ? 1U : 0U,__ATOMIC_RELEASE); }
static unsigned char accessibility_voice_running(void) { return (__atomic_load_n(&accessibility_state,__ATOMIC_ACQUIRE)&1)!=0; }
static unsigned char accessibility_reduce_transparency(void) { return 0; }
static void accessibility_post(unsigned notification,Word argument) {
    Word text=0,token=0;
    if(argument) { Word cls=read_word(argument);if(class_inherits(cls,external_class("_OBJC_CLASS_$_NSString")))text=argument;else if(class_inherits(cls,external_class("_OBJC_CLASS_$_UIView")))token=argument; }
    JNIEnv *env=attached_environment("Accessibility notification");static jmethodID method;
    if(!method){jclass type=(*env)->GetObjectClass(env,activity);method=(*env)->GetMethodID(env,type,"postGuestAccessibility","(IJLjava/lang/String;)V");(*env)->DeleteLocalRef(env,type);}
    jstring value=http_java_string(env,text);(*env)->CallVoidMethod(env,activity,method,(jint)notification,(jlong)token,value);if(value)(*env)->DeleteLocalRef(env,value);
    if((*env)->ExceptionCheck(env)){(*env)->ExceptionClear(env);stop("Accessibility notification failed","Android");}
}
static Word resolve_accessibility_import(const char *name) {
    if(!strcmp(name,"_UIAccessibilityIsVoiceOverRunning"))return (Word)accessibility_voice_running;
    if(!strcmp(name,"_UIAccessibilityIsReduceTransparencyEnabled"))return (Word)accessibility_reduce_transparency;
    if(!strcmp(name,"_UIAccessibilityPostNotification"))return (Word)accessibility_post;
    return 0;
}
static Method ui_system_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver) return NULL;
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSValue")) {
        if (class_receiver && !strcmp(selector,"valueWithCGRect:")) return (Method)value_rect;
        if (!class_receiver && !strcmp(selector,"CGRectValue")) return (Method)value_rect_get;
    }
    if (class_receiver && !strcmp(receiver,"_OBJC_CLASS_$_UNUserNotificationCenter") && !strcmp(selector,"currentNotificationCenter"))
        return (Method)current_user_notification_center;
    if (!strcmp(receiver,"_OBJC_CLASS_$_UIDevice") && !class_receiver) {
        if (!strcmp(selector,"orientation")) return (Method)device_orientation;
        if (!strcmp(selector,"userInterfaceIdiom")) return (Method)device_idiom;
        if (!strcmp(selector,"isMultitaskingSupported")) return (Method)device_multitasking;
        if (!strcmp(selector,"setBatteryMonitoringEnabled:")) return (Method)device_battery_monitor;
        if (!strcmp(selector,"isBatteryMonitoringEnabled")) return (Method)device_battery_monitored;
        if (!strcmp(selector,"batteryLevel")) return (Method)device_battery_level;
        if (!strcmp(selector,"batteryState")) return (Method)device_battery_state;
        if (!strcmp(selector,"beginGeneratingDeviceOrientationNotifications")) return (Method)orientation_begin;
        if (!strcmp(selector,"endGeneratingDeviceOrientationNotifications")) return (Method)orientation_end;
        if (!strcmp(selector,"isGeneratingDeviceOrientationNotifications")) return (Method)orientation_generating;
    }
    if (object==(Word)&process_info && !strcmp(selector,"isLowPowerModeEnabled")) return (Method)low_power_mode;
    if (!strcmp(receiver,"_OBJC_CLASS_$_UIScreen")) {
        if (class_receiver && !strcmp(selector,"mainScreen")) return (Method)main_screen;
        if (!class_receiver) {
            if (!strcmp(selector,"bounds")) return (Method)screen_bounds;
            if (!strcmp(selector,"nativeBounds")) return (Method)screen_native_bounds;
            if (!strcmp(selector,"scale") || !strcmp(selector,"nativeScale")) return (Method)screen_scale;
        }
    }
    if (!strcmp(receiver,"_OBJC_CLASS_$_UIUserNotificationSettings")) {
        if (class_receiver && !strcmp(selector,"settingsForTypes:categories:")) return (Method)notification_settings;
        if (!class_receiver && !strcmp(selector,"types")) return (Method)notification_types;
        if (!class_receiver && !strcmp(selector,"categories")) return (Method)notification_categories;
    }
    if (object==(Word)&application) {
        if (!strcmp(selector,"setDelegate:")) return (Method)application_set_delegate;
        if (!strcmp(selector,"isProtectedDataAvailable")) return (Method)application_protected_data;
        if (!strcmp(selector,"statusBarOrientation")) return (Method)interface_orientation;
        if (!strcmp(selector,"statusBarStyle")) return (Method)application_status_bar_style;
        if (!strcmp(selector,"setStatusBarStyle:")) return (Method)application_set_status_style_plain;
        if (!strcmp(selector,"setStatusBarStyle:animated:")) return (Method)application_set_status_style;
        if (!strcmp(selector,"isStatusBarHidden")) return (Method)application_status_bar_hidden;
        if (!strcmp(selector,"statusBarFrame")) return (Method)application_status_bar_frame;
        if (!strcmp(selector,"setStatusBarHidden:")) return (Method)application_set_status_hidden_plain;
        if (!strcmp(selector,"setStatusBarHidden:withAnimation:")) return (Method)application_set_status_hidden;
        if (!strcmp(selector,"setApplicationIconBadgeNumber:")) return (Method)application_set_badge;
        if (!strcmp(selector,"applicationIconBadgeNumber")) return (Method)application_get_badge;
        if (!strcmp(selector,"applicationState")) return (Method)application_state;
        if (!strcmp(selector,"registerForRemoteNotifications")) return (Method)register_remote_notifications;
        if (!strcmp(selector,"isRegisteredForRemoteNotifications")) return (Method)remote_notifications_registered;
        if (!strcmp(selector,"registerUserNotificationSettings:")) return (Method)register_user_notifications;
        if (!strcmp(selector,"currentUserNotificationSettings")) return (Method)get_user_notification_settings;
        if (!strcmp(selector,"cancelAllLocalNotifications")) return (Method)cancel_local_notifications;
        if (!strcmp(selector,"beginBackgroundTaskWithExpirationHandler:")) return (Method)begin_background_task;
        if (!strcmp(selector,"beginBackgroundTaskWithName:expirationHandler:")) return (Method)begin_named_background_task;
        if (!strcmp(selector,"endBackgroundTask:")) return (Method)end_background_task;
    }
    return NULL;
}
