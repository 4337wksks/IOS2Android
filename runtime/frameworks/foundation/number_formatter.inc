 
struct NumberFormatter { Word isa,locale,positive; long style,behavior; };
static void formatter_behavior(struct NumberFormatter *formatter,const char *selector,long behavior) {
    if (behavior!=0 && behavior!=1040 && behavior!=1000) stop("Unknown number formatter behavior",selector);
    if (behavior==1000) stop("Legacy pre-10.4 formatter unavailable",selector);
    formatter->behavior=behavior;
}
static Word formatter_get_behavior(struct NumberFormatter *formatter) { return formatter->behavior; }
static long numeric_compare(struct HostObject *first,const char *selector,struct HostObject *second) {
    if (second && second->isa==decimal_number_class) return decimal_compare_numbers(first,selector,second);
    if (!second || second->isa!=external_class("_OBJC_CLASS_$_NSNumber")) stop("Expected NSNumber",selector);
    long double a=first->content[1] ? (long double)numeric_double(first) : (long double)(intptr_t)first->content[0];
    long double b=second->content[1] ? (long double)numeric_double(second) : (long double)(intptr_t)second->content[0];
    if (isnan(a)) return isnan(b) ? 0 : -1;
    if (isnan(b)) return 1;
    return a<b ? -1 : a>b ? 1 : 0;
}
static Word numeric_equal(struct HostObject *first,const char *selector,struct HostObject *second) {
    return second && second->isa==external_class("_OBJC_CLASS_$_NSNumber") && numeric_compare(first,selector,second)==0;
}
static Word number_text(struct HostObject *number,const char *selector) {
    char text[96];
    if (number->content[1]) snprintf(text,sizeof(text),"%.17g",numeric_double(number));
    else snprintf(text,sizeof(text),"%lld",(long long)number->content[0]);
    return copy_string(text);
}
static void formatter_style(struct NumberFormatter *formatter,const char *selector,long style) {
    if (style<0 || style>4) stop("Unsupported number formatter style",selector);
    formatter->style=style;
}
static Word formatter_get_style(struct NumberFormatter *formatter) { return formatter->style; }
static void formatter_locale(struct NumberFormatter *formatter,const char *selector,Word locale) { formatter->locale=locale; }
static Word formatter_get_locale(struct NumberFormatter *formatter) { return formatter->locale ? formatter->locale : current_locale(external_class("_OBJC_CLASS_$_NSLocale")); }
static void formatter_pattern(struct NumberFormatter *formatter,const char *selector,Word pattern) { formatter->positive=pattern ? text_copy(pattern,"copy") : 0; }
static Word formatter_get_pattern(struct NumberFormatter *formatter) { return formatter->positive; }
static jobject formatter_java(JNIEnv *env,struct NumberFormatter *formatter,const char *selector) {
    jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID create=(*env)->GetMethodID(env,type,"numberFormatter","(Ljava/lang/String;ILjava/lang/String;)Landroid/icu/text/NumberFormat;");
    Word identifier=formatter->locale ? locale_component((void *)formatter->locale,"localeIdentifier") : 0;
    jstring locale=identifier ? http_java_string(env,identifier) : NULL,pattern=formatter->positive ? http_java_string(env,formatter->positive) : NULL;
    jobject result=(*env)->CallObjectMethod(env,activity,create,locale,(jint)formatter->style,pattern);
    if (locale) (*env)->DeleteLocalRef(env,locale); if (pattern) (*env)->DeleteLocalRef(env,pattern); (*env)->DeleteLocalRef(env,type);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return NULL; }
    return result;
}
static Word formatter_string(struct NumberFormatter *formatter,const char *selector,struct HostObject *number) {
    if (!number) return 0;
    JNIEnv *env=attached_environment(selector); jobject java=formatter_java(env,formatter,selector); if (!java) return 0;
    jclass type=(*env)->GetObjectClass(env,java);
    jstring text=NULL;
    if (number->isa==decimal_number_class) {
        struct DecimalValue value=decimal_load(number);
        if (!value.nan) {
             
            char bytes[192]; decimal_format(value,bytes);
            jclass decimal=(*env)->FindClass(env,"java/math/BigDecimal");
            jmethodID init=(*env)->GetMethodID(env,decimal,"<init>","(Ljava/lang/String;)V");
            jstring input=(*env)->NewStringUTF(env,bytes);
            jobject boxed=(*env)->NewObject(env,decimal,init,input);
            jmethodID format=(*env)->GetMethodID(env,type,"format","(Ljava/lang/Object;)Ljava/lang/String;");
            if (boxed && !(*env)->ExceptionCheck(env)) text=(*env)->CallObjectMethod(env,java,format,boxed);
            if (boxed) (*env)->DeleteLocalRef(env,boxed);
            (*env)->DeleteLocalRef(env,input); (*env)->DeleteLocalRef(env,decimal);
        }
    } else {
        jmethodID format=(*env)->GetMethodID(env,type,"format",number->content[1] ? "(D)Ljava/lang/String;" : "(J)Ljava/lang/String;");
        text=number->content[1] ? (*env)->CallObjectMethod(env,java,format,(jdouble)numeric_double(number)) : (*env)->CallObjectMethod(env,java,format,(jlong)number->content[0]);
    }
    Word result=text ? string_from_java(env,text,selector) : 0;
    if (text) (*env)->DeleteLocalRef(env,text); (*env)->DeleteLocalRef(env,type); (*env)->DeleteLocalRef(env,java);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return 0; }
    return result;
}
static Word formatter_number(struct NumberFormatter *formatter,const char *selector,Word string) {
    if (!string) return 0;
    JNIEnv *env=attached_environment(selector); jobject java=formatter_java(env,formatter,selector); if (!java) return 0;
    jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID parse=(*env)->GetMethodID(env,type,"parseFormattedNumber","(Landroid/icu/text/NumberFormat;Ljava/lang/String;)Ljava/lang/String;");
    jstring input=http_java_string(env,string),text=(*env)->CallObjectMethod(env,activity,parse,java,input);
    Word result=0;
    if (text) {
        const char *value=(*env)->GetStringUTFChars(env,text,NULL);
        if (strpbrk(value,".eE") || !strcmp(value,"NaN") || strstr(value,"Infinity")) result=real_number_factory(external_class("_OBJC_CLASS_$_NSNumber"),selector,strtod(value,NULL));
        else result=number_integer(strtoll(value,NULL,10));
        (*env)->ReleaseStringUTFChars(env,text,value); (*env)->DeleteLocalRef(env,text);
    }
    (*env)->DeleteLocalRef(env,input); (*env)->DeleteLocalRef(env,java); (*env)->DeleteLocalRef(env,type);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return 0; }
    return result;
}
static Method number_formatter_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (class_receiver || !receiver) return NULL;
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSNumber") && !strcmp(selector,"stringValue")) return (Method)number_text;
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSNumber") && !strcmp(selector,"compare:")) return (Method)numeric_compare;
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSNumber") && (!strcmp(selector,"isEqualToNumber:") || !strcmp(selector,"isEqual:"))) return (Method)numeric_equal;
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSNumberFormatter")) {
        if (!strcmp(selector,"setFormatterBehavior:")) return (Method)formatter_behavior;
        if (!strcmp(selector,"formatterBehavior")) return (Method)formatter_get_behavior;
        if (!strcmp(selector,"setNumberStyle:")) return (Method)formatter_style;
        if (!strcmp(selector,"numberStyle")) return (Method)formatter_get_style;
        if (!strcmp(selector,"setLocale:")) return (Method)formatter_locale;
        if (!strcmp(selector,"locale")) return (Method)formatter_get_locale;
        if (!strcmp(selector,"setPositiveFormat:")) return (Method)formatter_pattern;
        if (!strcmp(selector,"positiveFormat")) return (Method)formatter_get_pattern;
        if (!strcmp(selector,"stringFromNumber:") || !strcmp(selector,"stringForObjectValue:")) return (Method)formatter_string;
        if (!strcmp(selector,"numberFromString:")) return (Method)formatter_number;
    }
    return NULL;
}
