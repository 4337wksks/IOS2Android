 
struct RegularExpression { Word isa,pattern,options; jobject compiled; };
struct RegexMatch { Word isa,expression,count,flags; struct GuestRange ranges[]; };
static jstring regex_string(JNIEnv *env,Word string,const char *selector) {
    struct TextView text=text_view(string,selector);
    jstring result=java_string(env,text,selector); free(text.owned); return result;
}
static Word regex_exception(JNIEnv *env,const char *selector) {
    jthrowable exception=(*env)->ExceptionOccurred(env);
    (*env)->ExceptionClear(env);
    jclass type=(*env)->GetObjectClass(env,exception);
    jmethodID description=(*env)->GetMethodID(env,type,"toString","()Ljava/lang/String;");
    jstring text=(*env)->CallObjectMethod(env,exception,description);
    Word message=string_from_java(env,text,selector);
    (*env)->DeleteLocalRef(env,text); (*env)->DeleteLocalRef(env,type); (*env)->DeleteLocalRef(env,exception);
    return message;
}
static Word regex_create(Word cls,const char *selector,Word pattern,Word options,Word *error) {
    if (error) *error=0;
    if (!pattern || (options&~127UL)) stop("Invalid regular expression options",selector);
    JNIEnv *env=attached_environment(selector);
    jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID compile=(*env)->GetMethodID(env,type,"compileExpression","(Ljava/lang/String;I)Ljava/util/regex/Pattern;");
    jstring text=regex_string(env,pattern,selector);
    jobject compiled=(*env)->CallObjectMethod(env,activity,compile,text,(jint)options);
    (*env)->DeleteLocalRef(env,text); (*env)->DeleteLocalRef(env,type);
    if ((*env)->ExceptionCheck(env)) {
        Word message=regex_exception(env,selector);
        if (error) {
            struct GuestMap *info=(void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));
            map_set(info,selector,message,copy_string("NSLocalizedDescription"));
            *error=error_with_domain(external_class("_OBJC_CLASS_$_NSError"),selector,copy_string("NSCocoaErrorDomain"),2048,(Word)info);
        }
        return 0;
    }
    struct RegularExpression *expression=calloc(1,sizeof(*expression));
    if (!expression || !compiled) stop("Allocation failed",selector);
    expression->isa=cls; expression->pattern=text_copy(pattern,"copy"); expression->options=options;
    expression->compiled=(*env)->NewGlobalRef(env,compiled); (*env)->DeleteLocalRef(env,compiled);
    return (Word)expression;
}
static Word regex_init(Word object,const char *selector,Word pattern,Word options,Word *error) {
    Word result=regex_create(read_word(object),selector,pattern,options,error); free((void *)object); return result;
}
static Word predicate_regex_create(Word cls,const char *selector,Word pattern,unsigned flags) {
    Word error=0,expression=regex_create(external_class("_OBJC_CLASS_$_NSRegularExpression"),selector,pattern,flags,&error);
    if (!expression) stop("Invalid predicate regular expression",selector);
    struct HostObject *predicate=(void *)host_allocate(cls); predicate->content[0]=expression; predicate->content[1]=7;
    return (Word)predicate;
}
static Word predicate_regex_evaluate(struct HostObject *predicate,const char *selector,Word object) {
    if (!object) return 0;
    struct RegularExpression *expression=(void *)predicate->content[0];
    JNIEnv *env=attached_environment(selector);
    jclass pattern_type=(*env)->GetObjectClass(env,expression->compiled);
    jmethodID matcher_method=(*env)->GetMethodID(env,pattern_type,"matcher","(Ljava/lang/CharSequence;)Ljava/util/regex/Matcher;");
    jstring text=regex_string(env,object,selector);
    jobject matcher=(*env)->CallObjectMethod(env,expression->compiled,matcher_method,text);
    jclass matcher_type=matcher ? (*env)->GetObjectClass(env,matcher) : NULL;
    jmethodID matches=matcher_type ? (*env)->GetMethodID(env,matcher_type,"matches","()Z") : NULL;
    Word result=matches ? (*env)->CallBooleanMethod(env,matcher,matches) : 0;
    if (matcher_type) (*env)->DeleteLocalRef(env,matcher_type);
    if (matcher) (*env)->DeleteLocalRef(env,matcher);
    (*env)->DeleteLocalRef(env,text); (*env)->DeleteLocalRef(env,pattern_type);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); stop("Predicate regex evaluation failed",selector); }
    return result;
}
static Word regex_pattern(struct RegularExpression *object) { return object->pattern; }
static Word regex_options(struct RegularExpression *object) { return object->options; }
static Word regex_matches(struct RegularExpression *expression,const char *selector,Word string,Word options,struct GuestRange range) {
    check_range(range,string_length(string,selector),selector);
    if (range.location>INT32_MAX || range.length>INT32_MAX || (options&~31UL)) stop("Unsupported regex match options",selector);
    JNIEnv *env=attached_environment(selector);
    jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID matches=(*env)->GetMethodID(env,type,"expressionMatches","(Ljava/util/regex/Pattern;Ljava/lang/String;IIII)[J");
    jstring text=regex_string(env,string,selector);
    int first=!strcmp(selector,"firstMatchInString:options:range:") || !strcmp(selector,"rangeOfFirstMatchInString:options:range:");
    jlongArray result=(*env)->CallObjectMethod(env,activity,matches,expression->compiled,text,(jint)options,(jint)range.location,(jint)range.length,first ? 1 : 0);
    (*env)->DeleteLocalRef(env,text); (*env)->DeleteLocalRef(env,type);
    if ((*env)->ExceptionCheck(env)) { Word message=regex_exception(env,selector); stop("Regex matching failed",string_bytes(message,selector)); }
    jlong header[3]; (*env)->GetLongArrayRegion(env,result,0,3,header);
    size_t groups=(size_t)header[0],count=(size_t)header[1];
    struct Sequence *array=(void *)sequence_empty(external_class("_OBJC_CLASS_$_NSArray"));
    jlong *values=(*env)->GetLongArrayElements(env,result,NULL);
    if (!values) stop("Allocation failed",selector);
    for (size_t i=0;i<count;++i) {
        struct RegexMatch *match=calloc(1,sizeof(*match)+groups*sizeof(*match->ranges));
        if (!match) stop("Allocation failed",selector);
        match->isa=external_class("_OBJC_CLASS_$_NSTextCheckingResult"); match->expression=(Word)expression; match->count=groups;
        jlong *row=values+3+i*(groups*2+1); match->flags=(Word)row[0];
        for (size_t j=0;j<groups;++j) match->ranges[j]=(struct GuestRange){row[1+j*2]<0 ? INT64_MAX : (Word)row[1+j*2],(Word)row[2+j*2]};
        sequence_add(array,selector,(Word)match);
    }
    (*env)->ReleaseLongArrayElements(env,result,values,JNI_ABORT); (*env)->DeleteLocalRef(env,result);
    return (Word)array;
}
static Word regex_first(struct RegularExpression *expression,const char *selector,Word string,Word options,struct GuestRange range) {
    struct Sequence *matches=(void *)regex_matches(expression,selector,string,options,range);
    return matches->count ? matches->items[0] : 0;
}
static Word regex_count(struct RegularExpression *expression,const char *selector,Word string,Word options,struct GuestRange range) { return ((struct Sequence *)regex_matches(expression,selector,string,options,range))->count; }
static struct GuestRange regex_first_range(struct RegularExpression *expression,const char *selector,Word string,Word options,struct GuestRange range) {
    struct RegexMatch *match=(void *)regex_first(expression,selector,string,options,range);
    return match ? match->ranges[0] : no_range;
}
static struct GuestRange regex_range(struct RegexMatch *match) { return match->ranges[0]; }
static struct GuestRange regex_range_at(struct RegexMatch *match,const char *selector,Word index) {
    if (index>=match->count) stop("Invalid capture group",selector);
    return match->ranges[index];
}
static Word regex_match_count(struct RegexMatch *match) { return match->count; }
static Word regex_match_expression(struct RegexMatch *match) { return match->expression; }
static Word regex_result_type(void) { return 1024; }
static Word regex_replace(struct RegularExpression *expression,const char *selector,Word string,Word options,struct GuestRange range,Word replacement) {
    check_range(range,string_length(string,selector),selector);
    if (range.location>INT32_MAX || range.length>INT32_MAX || (options&~31UL)) stop("Unsupported regex match options",selector);
    JNIEnv *env=attached_environment(selector);
    jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID replace=(*env)->GetMethodID(env,type,"expressionReplace","(Ljava/util/regex/Pattern;Ljava/lang/String;IIILjava/lang/String;)Ljava/lang/String;");
    jstring text=regex_string(env,string,selector),template=regex_string(env,replacement,selector);
    jstring result=(*env)->CallObjectMethod(env,activity,replace,expression->compiled,text,(jint)options,(jint)range.location,(jint)range.length,template);
    (*env)->DeleteLocalRef(env,text); (*env)->DeleteLocalRef(env,template); (*env)->DeleteLocalRef(env,type);
    if ((*env)->ExceptionCheck(env)) { Word message=regex_exception(env,selector); stop("Regex replacement failed",string_bytes(message,selector)); }
    Word value=string_from_java(env,result,selector); (*env)->DeleteLocalRef(env,result); return value;
}
static Method regular_expression_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver) return NULL;
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSRegularExpression")) {
        if (class_receiver && !strcmp(selector,"regularExpressionWithPattern:options:error:")) return (Method)regex_create;
        if (!class_receiver) {
            if (!strcmp(selector,"initWithPattern:options:error:")) return (Method)regex_init;
            if (!strcmp(selector,"pattern")) return (Method)regex_pattern;
            if (!strcmp(selector,"options")) return (Method)regex_options;
            if (!strcmp(selector,"matchesInString:options:range:")) return (Method)regex_matches;
            if (!strcmp(selector,"firstMatchInString:options:range:")) return (Method)regex_first;
            if (!strcmp(selector,"rangeOfFirstMatchInString:options:range:")) return (Method)regex_first_range;
            if (!strcmp(selector,"numberOfMatchesInString:options:range:")) return (Method)regex_count;
            if (!strcmp(selector,"stringByReplacingMatchesInString:options:range:withTemplate:")) return (Method)regex_replace;
            if (!strcmp(selector,"copy")) return (Method)identity;
        }
    }
    if (!class_receiver && !strcmp(receiver,"_OBJC_CLASS_$_NSTextCheckingResult")) {
        if (!strcmp(selector,"range")) return (Method)regex_range;
        if (!strcmp(selector,"rangeAtIndex:")) return (Method)regex_range_at;
        if (!strcmp(selector,"numberOfRanges")) return (Method)regex_match_count;
        if (!strcmp(selector,"regularExpression")) return (Method)regex_match_expression;
        if (!strcmp(selector,"resultType")) return (Method)regex_result_type;
        if (!strcmp(selector,"copy")) return (Method)identity;
    }
    return NULL;
}
