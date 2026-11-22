 

struct CalendarComponents { long year, month, day, hour, minute, second; };
static const char *const component_setters[]={"setYear:","setMonth:","setDay:","setHour:","setMinute:","setSecond:"};
static const char *const component_getters[]={"year","month","day","hour","minute","second"};

static struct CalendarComponents *date_components(struct HostObject *object) {
    if (!object->content[0]) {
        struct CalendarComponents *components=malloc(sizeof(*components));
        if (!components) stop("Allocation failed","NSDateComponents");
        *components=(struct CalendarComponents){INT64_MAX,INT64_MAX,INT64_MAX,INT64_MAX,INT64_MAX,INT64_MAX};
        object->content[0]=(Word)components;
    }
    return (void *)object->content[0];
}

static void set_date_component(struct HostObject *self,const char *selector,long value) {
    long *components=(void *)date_components(self);
    for (unsigned index=0;index<COUNT(component_setters);++index)
        if (!strcmp(component_setters[index],selector)) { components[index]=value; return; }
}

static Word get_date_component(struct HostObject *self,const char *selector) {
    long *components=(void *)date_components(self);
    for (unsigned index=0;index<COUNT(component_getters);++index)
        if (!strcmp(component_getters[index],selector)) return components[index];
    return INT64_MAX;
}

static Word timezone_abbreviation(Word cls,const char *selector,Word abbreviation) {
    if (!abbreviation) return 0;
    JNIEnv *env=attached_environment(selector);
    struct TextView text=text_view(abbreviation,selector);
    jstring name=java_string(env,text,selector);
    free(text.owned);
    jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID resolve=(*env)->GetMethodID(env,type,"resolveTimeZone","(Ljava/lang/String;)Ljava/lang/String;");
    jstring identifier=(*env)->CallObjectMethod(env,activity,resolve,name);
    (*env)->DeleteLocalRef(env,name); (*env)->DeleteLocalRef(env,type);
    if (!identifier) return 0;
    Word resolved=string_from_java(env,identifier,selector);
    (*env)->DeleteLocalRef(env,identifier);
    struct HostObject *zone=calloc(1,sizeof(*zone));
    if (!zone) stop("Allocation failed",selector);
    zone->isa=cls; zone->content[0]=resolved; zone->content[2]=1;
    return (Word)zone;
}
static Word default_timezone(Word cls,const char *selector) { return timezone_abbreviation(cls,selector,system_text("timezone")); }
static Word timezone_fixed(Word cls,const char *selector,long seconds) {
    struct HostObject *zone=(void *)host_allocate(cls);
    zone->content[0]=copy_string("GMT"); zone->content[1]=(Word)seconds; zone->content[2]=2;
    return (Word)zone;
}
static Word timezone_name(struct HostObject *zone) { return zone->content[0]; }
static double scheduling_now(void);
static Word timezone_display_abbreviation_date(struct HostObject *zone,const char *selector,Word date) {
    double seconds=date ? date_seconds(date) : scheduling_now();
    JNIEnv *env=attached_environment(selector); jclass cls=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,cls,"timezoneAbbreviation","(Ljava/lang/String;JI)Ljava/lang/String;");
    struct TextView text=text_view(zone->content[0],selector); jstring name=java_string(env,text,selector); free(text.owned);
    jstring abbreviation=(*env)->CallObjectMethod(env,activity,method,name,(jlong)(seconds*1000),
        zone->content[2]==2 ? (jint)zone->content[1] : (jint)INT32_MIN);
    Word result=abbreviation ? string_from_java(env,abbreviation,selector) : 0;
    if (abbreviation) (*env)->DeleteLocalRef(env,abbreviation);
    (*env)->DeleteLocalRef(env,name); (*env)->DeleteLocalRef(env,cls); return result;
}
static Word timezone_display_abbreviation(struct HostObject *zone,const char *selector) {
    return timezone_display_abbreviation_date(zone,selector,0);
}

static Word calendar_init(struct HostObject *self,const char *selector,Word identifier) {
    const char *name=string_bytes(identifier,selector);
    if (strcmp(name,"gregorian")) stop("Unsupported calendar",name);
    self->content[0]=identifier;
    return (Word)self;
}

static void calendar_timezone(struct HostObject *self,const char *selector,Word zone) { self->content[1]=zone; }

static Word date_epoch(Word cls,const char *selector,double seconds) {
    struct HostObject *date=calloc(1,sizeof(*date));
    if (!date) stop("Allocation failed",selector);
    date->isa=cls;
    memcpy(date->content,&seconds,sizeof(seconds));
    return (Word)date;
}

static Word date_init(Word self) {
    struct timespec now;
    clock_gettime(CLOCK_REALTIME,&now);
    double seconds=now.tv_sec+now.tv_nsec*1e-9;
    memcpy(((struct HostObject *)self)->content,&seconds,sizeof(seconds));
    return self;
}

static Word calendar_date(struct HostObject *calendar,const char *selector,struct HostObject *object) {
    const struct CalendarComponents *components=date_components(object);
    struct tm value={
        .tm_year=components->year==INT64_MAX ? -1899 : components->year-1900,
        .tm_mon=components->month==INT64_MAX ? 0 : components->month-1,
        .tm_mday=components->day==INT64_MAX ? 1 : components->day,
        .tm_hour=components->hour==INT64_MAX ? 0 : components->hour,
        .tm_min=components->minute==INT64_MAX ? 0 : components->minute,
        .tm_sec=components->second==INT64_MAX ? 0 : components->second
    };
    struct HostObject *zone=(void *)calendar->content[1];
    if (!zone) zone=(void *)default_timezone(external_class("_OBJC_CLASS_$_NSTimeZone"),selector);
    if (!zone) return 0;
    double seconds;
    if (zone->content[2]==2) seconds=(double)timegm(&value)-(intptr_t)zone->content[1];
    else {
        JNIEnv *env=attached_environment(selector);
        struct TextView text=text_view(zone->content[0],selector);
        jstring name=java_string(env,text,selector);
        free(text.owned);
        jclass type=(*env)->GetObjectClass(env,activity);
        jmethodID convert=(*env)->GetMethodID(env,type,"dateFromComponents","(IIIIIILjava/lang/String;)D");
        seconds=(*env)->CallDoubleMethod(env,activity,convert,(jint)value.tm_year+1900,(jint)value.tm_mon+1,(jint)value.tm_mday,
            (jint)value.tm_hour,(jint)value.tm_min,(jint)value.tm_sec,name);
        (*env)->DeleteLocalRef(env,name); (*env)->DeleteLocalRef(env,type);
        if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return 0; }
    }
    return date_epoch(external_class("_OBJC_CLASS_$_NSDate"),selector,seconds);
}

static Word date_compare(Word self,const char *selector,Word other) {
    if (!other) stop("Nil date comparison",selector);
    double first=date_seconds(self), second=date_seconds(other);
    return (Word)(intptr_t)(first<second ? -1 : first>second);
}

static double date_since_reference(Word self) { return date_seconds(self)-978307200; }

struct DateFormatter { Word isa,pattern,locale,timezone; unsigned behavior,lenient; };
_Static_assert(sizeof(struct DateFormatter)<=sizeof(struct HostObject),"Date formatter allocation size");
static void date_formatter_pattern(struct DateFormatter *self,const char *selector,Word pattern) {
    self->pattern=pattern ? text_copy(pattern,"copy") : 0;
}
static Word date_formatter_get_pattern(struct DateFormatter *self) { return self->pattern ? self->pattern : copy_string(""); }
static void date_formatter_locale(struct DateFormatter *self,const char *selector,Word value) { self->locale=value; }
static Word date_formatter_get_locale(struct DateFormatter *self) { return self->locale ? self->locale : current_locale(external_class("_OBJC_CLASS_$_NSLocale")); }
static void date_formatter_timezone(struct DateFormatter *self,const char *selector,Word value) { self->timezone=value; }
static Word date_formatter_get_timezone(struct DateFormatter *self) { return self->timezone ? self->timezone : default_timezone(external_class("_OBJC_CLASS_$_NSTimeZone"),"defaultTimeZone"); }
static void date_formatter_behavior(struct DateFormatter *self,const char *selector,Word value) {
    if (value!=0 && value!=1040) stop("Unsupported date formatter behavior",selector);
    self->behavior=(unsigned)value;
}
static Word date_formatter_get_behavior(struct DateFormatter *self) { return self->behavior; }
static void date_formatter_lenient(struct DateFormatter *self,const char *selector,unsigned char value) { self->lenient=value!=0; }
static Word date_formatter_get_lenient(struct DateFormatter *self) { return self->lenient; }
static Word date_formatter_copy(struct DateFormatter *self) {
    struct DateFormatter *result=(void *)host_allocate(self->isa); *result=*self; return (Word)result;
}
static void date_java_check(JNIEnv *env,const char *selector) {
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); stop("Android date formatter failed",selector); }
}
static jclass date_java_class(JNIEnv *env,const char *name,const char *selector) {
    jclass type=(*env)->FindClass(env,name); date_java_check(env,selector);
    if (!type) stop("Android date formatter class unavailable",name);
    return type;
}
static jmethodID date_java_method(JNIEnv *env,jclass type,const char *name,const char *signature,int class_method,const char *selector) {
    jmethodID method=class_method ? (*env)->GetStaticMethodID(env,type,name,signature) : (*env)->GetMethodID(env,type,name,signature);
    date_java_check(env,selector);
    if (!method) stop("Android date formatter method unavailable",name);
    return method;
}
static jstring date_java_text(JNIEnv *env,Word value,const char *selector) {
    if (!value) return NULL;
    struct TextView text=text_view(value,selector);
    jstring result=java_string(env,text,selector); free(text.owned); date_java_check(env,selector); return result;
}
static jobject date_formatter_java(JNIEnv *env,struct DateFormatter *self,const char *selector) {
    jclass locale_type=date_java_class(env,"android/icu/util/ULocale",selector);
    Word identifier=self->locale ? ((struct HostObject *)self->locale)->content[0] : 0;
    jobject locale_value;
    if (identifier) {
        jmethodID create=date_java_method(env,locale_type,"createCanonical","(Ljava/lang/String;)Landroid/icu/util/ULocale;",1,selector);
        locale_value=(*env)->CallStaticObjectMethod(env,locale_type,create,date_java_text(env,identifier,selector));
    } else {
        jmethodID current=date_java_method(env,locale_type,"getDefault","()Landroid/icu/util/ULocale;",1,selector);
        locale_value=(*env)->CallStaticObjectMethod(env,locale_type,current);
    }
    date_java_check(env,selector);
    jclass type=date_java_class(env,"android/icu/text/SimpleDateFormat",selector);
    jmethodID create=date_java_method(env,type,"<init>","(Ljava/lang/String;Landroid/icu/util/ULocale;)V",0,selector);
    jstring pattern=self->pattern ? date_java_text(env,self->pattern,selector) : (*env)->NewStringUTF(env,"");
    jobject formatter=(*env)->NewObject(env,type,create,pattern,locale_value); date_java_check(env,selector);
    if (!formatter) stop("Allocation failed",selector);
    jmethodID lenient=date_java_method(env,type,"setLenient","(Z)V",0,selector);
    (*env)->CallVoidMethod(env,formatter,lenient,(jboolean)self->lenient); date_java_check(env,selector);
    jclass zone_type=date_java_class(env,"android/icu/util/TimeZone",selector);
    struct HostObject *zone=(void *)self->timezone;
    jobject zone_value;
    if (zone) {
        jmethodID resolve=date_java_method(env,zone_type,"getTimeZone","(Ljava/lang/String;)Landroid/icu/util/TimeZone;",1,selector);
        zone_value=(*env)->CallStaticObjectMethod(env,zone_type,resolve,date_java_text(env,zone->content[0],selector));
        date_java_check(env,selector);
        if (zone->content[2]==2) {
            int64_t offset=(int64_t)zone->content[1];
            if (offset<INT32_MIN/1000 || offset>INT32_MAX/1000) stop("Time zone offset out of range",selector);
            jmethodID set_offset=date_java_method(env,zone_type,"setRawOffset","(I)V",0,selector);
            (*env)->CallVoidMethod(env,zone_value,set_offset,(jint)(offset*1000)); date_java_check(env,selector);
        }
    } else {
        jmethodID current=date_java_method(env,zone_type,"getDefault","()Landroid/icu/util/TimeZone;",1,selector);
        zone_value=(*env)->CallStaticObjectMethod(env,zone_type,current); date_java_check(env,selector);
    }
    jmethodID set_zone=date_java_method(env,type,"setTimeZone","(Landroid/icu/util/TimeZone;)V",0,selector);
    (*env)->CallVoidMethod(env,formatter,set_zone,zone_value); date_java_check(env,selector);
    return formatter;
}
static Word date_formatter_string(struct DateFormatter *self,const char *selector,Word date) {
    if (!date) return 0;
    if (read_word(date)!=external_class("_OBJC_CLASS_$_NSDate")) {
        if (!strcmp(selector,"stringForObjectValue:")) return 0;
        stop("Expected NSDate",selector);
    }
    double milliseconds=date_seconds(date)*1000;
    if (!isfinite(milliseconds) || milliseconds<-9223372036854775808.0 || milliseconds>=9223372036854775808.0) return 0;
    JNIEnv *env=attached_environment(selector);
    if ((*env)->PushLocalFrame(env,24)<0) { (*env)->ExceptionClear(env); stop("Allocation failed",selector); }
    jobject formatter=date_formatter_java(env,self,selector);
    jclass date_type=date_java_class(env,"java/util/Date",selector);
    jmethodID create=date_java_method(env,date_type,"<init>","(J)V",0,selector);
    jobject value=(*env)->NewObject(env,date_type,create,(jlong)floor(milliseconds)); date_java_check(env,selector);
    jclass type=date_java_class(env,"android/icu/text/DateFormat",selector);
    jmethodID format=date_java_method(env,type,"format","(Ljava/util/Date;)Ljava/lang/String;",0,selector);
    jstring text=(*env)->CallObjectMethod(env,formatter,format,value); date_java_check(env,selector);
    Word result=text ? string_from_java(env,text,selector) : 0;
    (*env)->PopLocalFrame(env,NULL); return result;
}
static Word date_formatter_parse(struct DateFormatter *self,const char *selector,Word string) {
    if (!string) return 0;
    JNIEnv *env=attached_environment(selector);
    if ((*env)->PushLocalFrame(env,24)<0) { (*env)->ExceptionClear(env); stop("Allocation failed",selector); }
    jobject formatter=date_formatter_java(env,self,selector);
    jclass position_type=date_java_class(env,"java/text/ParsePosition",selector);
    jmethodID create=date_java_method(env,position_type,"<init>","(I)V",0,selector);
    jobject position=(*env)->NewObject(env,position_type,create,0); date_java_check(env,selector);
    jclass type=date_java_class(env,"android/icu/text/DateFormat",selector);
    jmethodID parse=date_java_method(env,type,"parse","(Ljava/lang/String;Ljava/text/ParsePosition;)Ljava/util/Date;",0,selector);
    jstring text=date_java_text(env,string,selector);
    jobject date=(*env)->CallObjectMethod(env,formatter,parse,text,position); date_java_check(env,selector);
    jmethodID get_index=date_java_method(env,position_type,"getIndex","()I",0,selector);
    jint consumed=(*env)->CallIntMethod(env,position,get_index); date_java_check(env,selector);
    Word result=0;
    if (date && consumed>0 && consumed==(*env)->GetStringLength(env,text)) {
        jclass date_type=date_java_class(env,"java/util/Date",selector);
        jmethodID get_time=date_java_method(env,date_type,"getTime","()J",0,selector);
        jlong milliseconds=(*env)->CallLongMethod(env,date,get_time); date_java_check(env,selector);
        result=date_epoch(external_class("_OBJC_CLASS_$_NSDate"),selector,(double)milliseconds/1000);
    }
    (*env)->PopLocalFrame(env,NULL); return result;
}

static Method calendar_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver) return NULL;
    if (!class_receiver && !strcmp(receiver,"_OBJC_CLASS_$_NSDateFormatter")) {
        if (!strcmp(selector,"setDateFormat:")) return (Method)date_formatter_pattern;
        if (!strcmp(selector,"dateFormat")) return (Method)date_formatter_get_pattern;
        if (!strcmp(selector,"setLocale:")) return (Method)date_formatter_locale;
        if (!strcmp(selector,"locale")) return (Method)date_formatter_get_locale;
        if (!strcmp(selector,"setTimeZone:")) return (Method)date_formatter_timezone;
        if (!strcmp(selector,"timeZone")) return (Method)date_formatter_get_timezone;
        if (!strcmp(selector,"setFormatterBehavior:")) return (Method)date_formatter_behavior;
        if (!strcmp(selector,"formatterBehavior")) return (Method)date_formatter_get_behavior;
        if (!strcmp(selector,"setLenient:")) return (Method)date_formatter_lenient;
        if (!strcmp(selector,"isLenient")) return (Method)date_formatter_get_lenient;
        if (!strcmp(selector,"copy") || !strcmp(selector,"copyWithZone:")) return (Method)date_formatter_copy;
        if (!strcmp(selector,"stringFromDate:") || !strcmp(selector,"stringForObjectValue:")) return (Method)date_formatter_string;
        if (!strcmp(selector,"dateFromString:")) return (Method)date_formatter_parse;
    }
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSDateComponents") && !class_receiver) {
        for (unsigned index=0;index<COUNT(component_setters);++index) {
            if (!strcmp(selector,component_setters[index])) return (Method)set_date_component;
            if (!strcmp(selector,component_getters[index])) return (Method)get_date_component;
        }
    }
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSTimeZone")) {
        if (class_receiver && (!strcmp(selector,"timeZoneWithAbbreviation:") || !strcmp(selector,"timeZoneWithName:"))) return (Method)timezone_abbreviation;
        if (class_receiver && (!strcmp(selector,"defaultTimeZone") || !strcmp(selector,"systemTimeZone") || !strcmp(selector,"localTimeZone"))) return (Method)default_timezone;
        if (class_receiver && !strcmp(selector,"timeZoneForSecondsFromGMT:")) return (Method)timezone_fixed;
        if (!class_receiver && !strcmp(selector,"name")) return (Method)timezone_name;
        if (!class_receiver && !strcmp(selector,"abbreviation")) return (Method)timezone_display_abbreviation;
        if (!class_receiver && !strcmp(selector,"abbreviationForDate:")) return (Method)timezone_display_abbreviation_date;
        if (!class_receiver && !strcmp(selector,"copy")) return (Method)identity;
    }
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSCalendar") && !class_receiver) {
        if (!strcmp(selector,"initWithCalendarIdentifier:")) return (Method)calendar_init;
        if (!strcmp(selector,"setTimeZone:")) return (Method)calendar_timezone;
        if (!strcmp(selector,"dateFromComponents:")) return (Method)calendar_date;
    }
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSDate")) {
        if (class_receiver && !strcmp(selector,"dateWithTimeIntervalSince1970:")) return (Method)date_epoch;
        if (!class_receiver && !strcmp(selector,"init")) return (Method)date_init;
        if (!class_receiver && !strcmp(selector,"compare:")) return (Method)date_compare;
        if (!class_receiver && !strcmp(selector,"copy")) return (Method)identity;
        if (!class_receiver && !strcmp(selector,"timeIntervalSinceReferenceDate")) return (Method)date_since_reference;
    }
    return NULL;
}
