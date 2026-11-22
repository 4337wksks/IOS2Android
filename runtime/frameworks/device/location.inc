/* Location availability follows this Android application's actual permission
 * and system-service state. No synthetic position is supplied. */
static Word location_authorization(void) {
    JNIEnv *env=attached_environment("CLLocationManager authorization"); jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID query=(*env)->GetMethodID(env,type,"locationAuthorization","()I");
    Word result=(Word)(*env)->CallIntMethod(env,activity,query); (*env)->DeleteLocalRef(env,type); return result;
}
static Word location_enabled(void) {
    JNIEnv *env=attached_environment("CLLocationManager availability"); jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID query=(*env)->GetMethodID(env,type,"locationEnabled","()Z");
    Word result=(*env)->CallBooleanMethod(env,activity,query); (*env)->DeleteLocalRef(env,type); return result;
}
static void location_set_delegate(struct HostObject *manager,const char *selector,Word delegate) { manager->content[0]=delegate; }
static Word location_delegate(struct HostObject *manager) { return manager->content[0]; }
static void location_denied(void *context) {
    struct HostObject *manager=context;
    Method callback=find_implementation(manager->content[0],"locationManager:didFailWithError:");
    if (callback) callback(manager->content[0],"locationManager:didFailWithError:",(Word)manager,unsupported_service_error("kCLErrorDomain",1,"Location permission is not granted to this Android application"),0,0);
}
static void location_start(Word object,const char *selector) {
    if (location_authorization()>=3) stop("Authorized location streaming is not implemented",selector);
    schedule_main_function(location_denied,(void *)object);
}
static Word location_no_position(void) { return 0; }
static Method location_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver || strcmp(receiver,"_OBJC_CLASS_$_CLLocationManager")) return NULL;
    if (!strcmp(selector,"authorizationStatus")) return (Method)location_authorization;
    if (class_receiver && !strcmp(selector,"locationServicesEnabled")) return (Method)location_enabled;
    if (!class_receiver) {
        if (!strcmp(selector,"setDelegate:")) return (Method)location_set_delegate;
        if (!strcmp(selector,"delegate")) return (Method)location_delegate;
        if (!strcmp(selector,"location")) return (Method)location_no_position;
        if (!strcmp(selector,"startUpdatingLocation") || !strcmp(selector,"requestLocation")) return (Method)location_start;
        if (!strcmp(selector,"stopUpdatingLocation")) return (Method)noop;
    }
    return NULL;
}
