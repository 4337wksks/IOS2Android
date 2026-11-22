 
static Word locale_from_identifier(Word cls,const char *selector,Word identifier) {
    if (!identifier) return 0;
    struct HostObject *object=(void *)host_allocate(cls); object->content[0]=text_copy(identifier,"copy");
    return (Word)object;
}
static Word locale_init_identifier(Word object,const char *selector,Word identifier) {
    Word result=locale_from_identifier(read_word(object),selector,identifier); free((void *)object); return result;
}
static Word locale_property(struct HostObject *object,const char *selector,Word key) {
    JNIEnv *env=attached_environment(selector); jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,type,"localeInfo","(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    jstring identifier=object->content[0] ? http_java_string(env,object->content[0]) : NULL,name=http_java_string(env,key);
    jstring value=(*env)->CallObjectMethod(env,activity,method,identifier,name);
    Word result=value ? string_from_java(env,value,selector) : 0;
    if (value) (*env)->DeleteLocalRef(env,value);
    if (identifier) (*env)->DeleteLocalRef(env,identifier);
    (*env)->DeleteLocalRef(env,name); (*env)->DeleteLocalRef(env,type);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); stop("Android locale query failed",selector); }
    return result;
}
static Word locale_component(struct HostObject *object,const char *selector) { return locale_property(object,selector,copy_string(selector)); }
static Word locale_system(Word cls,const char *selector) { return locale_from_identifier(cls,selector,copy_string("")); }
static Word locale_languages(Word cls,const char *selector) {
    JNIEnv *env=attached_environment(selector); jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,type,"preferredLanguages","()[Ljava/lang/String;");
    jobjectArray languages=(*env)->CallObjectMethod(env,activity,method);
    Word result=sequence_empty(external_class("_OBJC_CLASS_$_NSArray"));
    for (jsize i=0;languages && i<(*env)->GetArrayLength(env,languages);++i) {
        jstring language=(*env)->GetObjectArrayElement(env,languages,i);
        sequence_add((void *)result,selector,string_from_java(env,language,selector)); (*env)->DeleteLocalRef(env,language);
    }
    if (languages) (*env)->DeleteLocalRef(env,languages);
    (*env)->DeleteLocalRef(env,type); return result;
}
static Word locale_components(Word cls,const char *selector,Word identifier) {
    if (!identifier) stop("Nil locale identifier",selector);
    struct HostObject locale={.content={identifier}};
    const char *const keys[]={"kCFLocaleLanguageCode","kCFLocaleCountryCode","kCFLocaleScriptCode","kCFLocaleVariantCode"};
    struct GuestMap *components=(void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));
    for (unsigned i=0;i<COUNT(keys);++i) {
        Word key=copy_string(keys[i]),value=locale_property(&locale,selector,key);
        if (value && string_length(value,selector)) map_set(components,selector,value,key);
    }
    return (Word)components;
}
static Word locale_canonical_identifier(Word cls,const char *selector,Word identifier) {
    if (!identifier) return 0;
    JNIEnv *env=attached_environment(selector); jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID canonical=(*env)->GetMethodID(env,type,"canonicalLocaleIdentifier","(Ljava/lang/String;Z)Ljava/lang/String;");
    jstring input=http_java_string(env,identifier);
    jstring output=(*env)->CallObjectMethod(env,activity,canonical,input,(jboolean)!strcmp(selector,"canonicalLanguageIdentifierFromString:"));
    Word result=output ? string_from_java(env,output,selector) : 0;
    if (output) (*env)->DeleteLocalRef(env,output); (*env)->DeleteLocalRef(env,input); (*env)->DeleteLocalRef(env,type);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); stop("Android locale canonicalization failed",selector); }
    return result;
}
static Word locale_identifier_components(Word cls,const char *selector,struct GuestMap *components) {
    if (!components) stop("Nil locale components",selector);
    const char *const keys[]={"kCFLocaleLanguageCode","kCFLocaleCountryCode","kCFLocaleScriptCode","kCFLocaleVariantCode"};
    JNIEnv *env=attached_environment(selector); jclass type=(*env)->GetObjectClass(env,activity),string=(*env)->FindClass(env,"java/lang/String");
    jobjectArray parts=(*env)->NewObjectArray(env,4,string,NULL);
    for (unsigned i=0;i<COUNT(keys);++i) {
        Word value=map_get(components,selector,copy_string(keys[i])); jstring input=value ? http_java_string(env,value) : NULL;
        (*env)->SetObjectArrayElement(env,parts,i,input); if (input) (*env)->DeleteLocalRef(env,input);
    }
    jmethodID combine=(*env)->GetMethodID(env,type,"localeIdentifierFromComponents","([Ljava/lang/String;)Ljava/lang/String;");
    jstring output=(*env)->CallObjectMethod(env,activity,combine,parts); Word result=output ? string_from_java(env,output,selector) : 0;
    if (output) (*env)->DeleteLocalRef(env,output); (*env)->DeleteLocalRef(env,parts); (*env)->DeleteLocalRef(env,string); (*env)->DeleteLocalRef(env,type);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); stop("Invalid locale components",selector); } return result;
}
static Word locale_language_direction(Word cls,const char *selector,Word language) {
    if (!language || !string_length(language,selector)) return 0;
    JNIEnv *env=attached_environment(selector);
    jclass type=(*env)->FindClass(env,"android/icu/util/ULocale");
    if (!type) { (*env)->ExceptionClear(env); stop("Android ICU locale unavailable",selector); }
    jmethodID create=(*env)->GetStaticMethodID(env,type,"createCanonical","(Ljava/lang/String;)Landroid/icu/util/ULocale;");
    jmethodID orientation=(*env)->GetMethodID(env,type,!strcmp(selector,"lineDirectionForLanguage:") ? "getLineOrientation" : "getCharacterOrientation","()Ljava/lang/String;");
    if (!create || !orientation) { (*env)->ExceptionClear(env); stop("Android ICU orientation unavailable",selector); }
    jstring name=http_java_string(env,language);
    jobject value=(*env)->CallStaticObjectMethod(env,type,create,name);
    if ((*env)->ExceptionCheck(env) || !value) { (*env)->ExceptionClear(env); stop("Invalid language identifier",selector); }
    jstring direction=(*env)->CallObjectMethod(env,value,orientation);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); stop("Android language direction failed",selector); }
    Word result=0;
    if (direction) {
        const char *text=(*env)->GetStringUTFChars(env,direction,NULL);
        if (!text) { (*env)->ExceptionClear(env); stop("Allocation failed",selector); }
        if (!strcmp(text,"left-to-right")) result=1;
        else if (!strcmp(text,"right-to-left")) result=2;
        else if (!strcmp(text,"top-to-bottom")) result=3;
        else if (!strcmp(text,"bottom-to-top")) result=4;
        (*env)->ReleaseStringUTFChars(env,direction,text); (*env)->DeleteLocalRef(env,direction);
    }
    (*env)->DeleteLocalRef(env,value); (*env)->DeleteLocalRef(env,name); (*env)->DeleteLocalRef(env,type);
    return result;
}
static Word timezone_offset_date(struct HostObject *zone,const char *selector,Word date) {
    if (zone->content[2]==2) return zone->content[1];
    double seconds=date ? date_seconds(date) : scheduling_now();
    JNIEnv *env=attached_environment(selector); jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,type,"timezoneOffset","(Ljava/lang/String;J)I");
    jstring name=http_java_string(env,zone->content[0]);
    int value=(*env)->CallIntMethod(env,activity,method,name,(jlong)(seconds*1000));
    (*env)->DeleteLocalRef(env,name); (*env)->DeleteLocalRef(env,type); return (Word)(intptr_t)value;
}
static Word timezone_offset(struct HostObject *zone,const char *selector) { return timezone_offset_date(zone,selector,0); }
static Word text_compare_locale(Word object,const char *selector,Word other,Word options,struct GuestRange range,Word locale) {
    if (options&~0x3cfUL) stop("Unsupported string comparison options",selector);
    struct TextView source=text_view(object,selector),right=text_view(other,selector); check_range(range,source.length,selector);
    struct TextView left={.units=source.units+range.location,.length=range.length};
    JNIEnv *env=attached_environment(selector); jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID compare=(*env)->GetMethodID(env,type,"compareText","(Ljava/lang/String;Ljava/lang/String;ILjava/lang/String;)I");
    jstring first=java_string(env,left,selector),second=java_string(env,right,selector),name=NULL;
    if (locale) name=http_java_string(env,locale_component((void *)locale,"localeIdentifier"));
    int result=(*env)->CallIntMethod(env,activity,compare,first,second,(jint)options,name);
    (*env)->DeleteLocalRef(env,first); (*env)->DeleteLocalRef(env,second); if (name) (*env)->DeleteLocalRef(env,name); (*env)->DeleteLocalRef(env,type);
    free(source.owned); free(right.owned);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); stop("Android string comparison failed",selector); }
    return (Word)(intptr_t)(result<0 ? -1 : result>0);
}
static Word text_compare_range(Word object,const char *selector,Word other,Word options,struct GuestRange range) { return text_compare_locale(object,selector,other,options,range,0); }
static Word text_compare_options(Word object,const char *selector,Word other,Word options) { return text_compare_range(object,selector,other,options,(struct GuestRange){0,string_length(object,selector)}); }
static Word text_compare_localized(Word object,const char *selector,Word other) {
    Word options=!strcmp(selector,"localizedCaseInsensitiveCompare:") ? 1 : !strcmp(selector,"localizedStandardCompare:") ? (1|64|128|256) : 0;
    return text_compare_locale(object,selector,other,options,(struct GuestRange){0,string_length(object,selector)},(Word)&locale);
}
static Method locale_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver) return NULL;
    if (!class_receiver && (!strcmp(receiver,"_OBJC_CLASS_$_NSString") || !strcmp(receiver,"_OBJC_CLASS_$_NSMutableString"))) {
        if (!strcmp(selector,"localizedCompare:") || !strcmp(selector,"localizedCaseInsensitiveCompare:") || !strcmp(selector,"localizedStandardCompare:")) return (Method)text_compare_localized;
        if (!strcmp(selector,"compare:options:")) return (Method)text_compare_options;
        if (!strcmp(selector,"compare:options:range:")) return (Method)text_compare_range;
        if (!strcmp(selector,"compare:options:range:locale:")) return (Method)text_compare_locale;
    }
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSLocale")) {
        if (class_receiver) {
            if (!strcmp(selector,"characterDirectionForLanguage:") || !strcmp(selector,"lineDirectionForLanguage:")) return (Method)locale_language_direction;
            if (!strcmp(selector,"preferredLanguages")) return (Method)locale_languages;
            if (!strcmp(selector,"systemLocale")) return (Method)locale_system;
            if (!strcmp(selector,"currentLocale") || !strcmp(selector,"autoupdatingCurrentLocale")) return (Method)current_locale;
            if (!strcmp(selector,"localeWithLocaleIdentifier:")) return (Method)locale_from_identifier;
            if (!strcmp(selector,"componentsFromLocaleIdentifier:")) return (Method)locale_components;
            if (!strcmp(selector,"localeIdentifierFromComponents:")) return (Method)locale_identifier_components;
            if (!strcmp(selector,"canonicalLocaleIdentifierFromString:") || !strcmp(selector,"canonicalLanguageIdentifierFromString:")) return (Method)locale_canonical_identifier;
        } else {
            if (!strcmp(selector,"initWithLocaleIdentifier:")) return (Method)locale_init_identifier;
            if (!strcmp(selector,"objectForKey:")) return (Method)locale_property;
            if (!strcmp(selector,"localeIdentifier") || !strcmp(selector,"languageCode") || !strcmp(selector,"countryCode") || !strcmp(selector,"scriptCode") ||
                !strcmp(selector,"variantCode") || !strcmp(selector,"currencyCode") || !strcmp(selector,"currencySymbol") || !strcmp(selector,"decimalSeparator") || !strcmp(selector,"groupingSeparator")) return (Method)locale_component;
            if (!strcmp(selector,"copy")) return (Method)identity;
        }
    }
    if (!class_receiver && !strcmp(receiver,"_OBJC_CLASS_$_NSTimeZone")) {
        if (!strcmp(selector,"secondsFromGMT")) return (Method)timezone_offset;
        if (!strcmp(selector,"secondsFromGMTForDate:")) return (Method)timezone_offset_date;
    }
    return NULL;
}
