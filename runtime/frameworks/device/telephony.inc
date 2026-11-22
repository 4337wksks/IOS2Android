 
static Word telephony_value(Word object,const char *selector) {
    JNIEnv *env=attached_environment(selector); jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID query=(*env)->GetMethodID(env,type,"telephonyInfo","()[Ljava/lang/String;");
    jobjectArray values=(*env)->CallObjectMethod(env,activity,query);
    (*env)->DeleteLocalRef(env,type);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return 0; }
    if (!values) return 0;
    Word result=0;
    if (!strcmp(selector,"currentRadioAccessTechnology")) {
        jstring value=(*env)->GetObjectArrayElement(env,values,4);
        if (value) { result=string_from_java(env,value,selector); (*env)->DeleteLocalRef(env,value); }
    } else {
        struct HostObject *carrier=(void *)host_allocate(external_class("_OBJC_CLASS_$_CTCarrier"));
        for (unsigned i=0;i<4;++i) {
            jstring value=(*env)->GetObjectArrayElement(env,values,i);
            if (value) { carrier->content[i]=string_from_java(env,value,selector); (*env)->DeleteLocalRef(env,value); }
        }
        result=(Word)carrier;
    }
    (*env)->DeleteLocalRef(env,values); return result;
}
static Word carrier_value(struct HostObject *object,const char *selector) {
    return object->content[!strcmp(selector,"carrierName") ? 0 : !strcmp(selector,"mobileCountryCode") ? 1 : !strcmp(selector,"mobileNetworkCode") ? 2 : 3];
}
static Method telephony_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (class_receiver || !receiver) return NULL;
    if (!strcmp(receiver,"_OBJC_CLASS_$_CTTelephonyNetworkInfo") && (!strcmp(selector,"subscriberCellularProvider") || !strcmp(selector,"currentRadioAccessTechnology"))) return (Method)telephony_value;
    if (!strcmp(receiver,"_OBJC_CLASS_$_CTCarrier") && (!strcmp(selector,"carrierName") || !strcmp(selector,"mobileCountryCode") || !strcmp(selector,"mobileNetworkCode") || !strcmp(selector,"isoCountryCode"))) return (Method)carrier_value;
    return NULL;
}
