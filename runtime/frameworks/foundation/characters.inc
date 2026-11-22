#define CHARACTER_BYTES (0x110000U/8)
struct CharacterSet { Word isa; unsigned char bits[CHARACTER_BYTES]; };
static struct CharacterSet *predefined_characters[8];
static pthread_mutex_t character_mutex=PTHREAD_MUTEX_INITIALIZER;
static const char *const character_names[]={"letterCharacterSet","nonBaseCharacterSet","decimalDigitCharacterSet","alphanumericCharacterSet",
    "whitespaceCharacterSet","whitespaceAndNewlineCharacterSet","newlineCharacterSet","controlCharacterSet"};
static struct CharacterSet *characters_new(Word cls) {
    struct CharacterSet *set=calloc(1,sizeof(*set));
    if (!set) stop("Allocation failed","NSCharacterSet");
    set->isa=cls;
    return set;
}
static Word characters_predefined(Word cls,const char *selector) {
    unsigned kind=0;
    while (kind<COUNT(character_names) && strcmp(selector,character_names[kind])) ++kind;
    if (kind==COUNT(character_names)) stop("Unknown predefined character set",selector);
    pthread_mutex_lock(&character_mutex);
    if (!predefined_characters[kind]) {
        JNIEnv *env=attached_environment(selector);
        jclass type=(*env)->GetObjectClass(env,activity);
        jmethodID bitmap=(*env)->GetMethodID(env,type,"characterBitmap","(I)[B");
        jbyteArray bytes=(*env)->CallObjectMethod(env,activity,bitmap,(jint)kind);
        (*env)->DeleteLocalRef(env,type);
        if (!bytes || (*env)->GetArrayLength(env,bytes)!=CHARACTER_BYTES) {
            pthread_mutex_unlock(&character_mutex); stop("Unicode character database unavailable",selector);
        }
        struct CharacterSet *set=characters_new(external_class("_OBJC_CLASS_$_NSCharacterSet"));
        (*env)->GetByteArrayRegion(env,bytes,0,CHARACTER_BYTES,(jbyte *)set->bits);
        (*env)->DeleteLocalRef(env,bytes);
        predefined_characters[kind]=set;
    }
    struct CharacterSet *set=predefined_characters[kind];
    pthread_mutex_unlock(&character_mutex);
    if (cls==external_class("_OBJC_CLASS_$_NSMutableCharacterSet")) {
        struct CharacterSet *copy=characters_new(cls); memcpy(copy->bits,set->bits,CHARACTER_BYTES); return (Word)copy;
    }
    return (Word)set;
}
static unsigned text_scalar(struct TextView text,size_t index,size_t *width) {
    unsigned ch=text.units[index]; *width=1;
    if (ch>=0xd800 && ch<=0xdbff && index+1<text.length && text.units[index+1]>=0xdc00 && text.units[index+1]<=0xdfff) {
        ch=0x10000+((ch-0xd800)<<10)+(text.units[index+1]-0xdc00); *width=2;
    }
    return ch;
}
static Word character_member(struct CharacterSet *set,const char *selector,uint32_t character) {
    return character<0x110000 && (set->bits[character>>3]&(1U<<(character&7)))!=0;
}
static void characters_change(struct CharacterSet *set,const char *selector,Word string) {
    if (set->isa!=external_class("_OBJC_CLASS_$_NSMutableCharacterSet")) stop("Mutating immutable character set",selector);
    struct TextView text=text_view(string,selector);
    int remove=!strcmp(selector,"removeCharactersInString:");
    for (size_t index=0;index<text.length;) {
        size_t width; unsigned ch=text_scalar(text,index,&width); index+=width;
        if (remove) set->bits[ch>>3]&=(unsigned char)~(1U<<(ch&7)); else set->bits[ch>>3]|=1U<<(ch&7);
    }
    free(text.owned);
}
static Word characters_from_string(Word cls,const char *selector,Word string) {
    struct CharacterSet *set=characters_new(external_class("_OBJC_CLASS_$_NSMutableCharacterSet"));
    characters_change(set,selector,string); set->isa=cls;
    return (Word)set;
}
static Word characters_copy(struct CharacterSet *set,const char *selector) {
    int mutable=!strcmp(selector,"mutableCopy") || !strcmp(selector,"mutableCopyWithZone:");
    struct CharacterSet *result=characters_new(external_class(mutable ? "_OBJC_CLASS_$_NSMutableCharacterSet" : "_OBJC_CLASS_$_NSCharacterSet"));
    int invert=!strcmp(selector,"invertedSet");
    for (size_t i=0;i<CHARACTER_BYTES;++i) result->bits[i]=invert ? (unsigned char)~set->bits[i] : set->bits[i];
    return (Word)result;
}
static void characters_combine(struct CharacterSet *set,const char *selector,struct CharacterSet *other) {
    if (set->isa!=external_class("_OBJC_CLASS_$_NSMutableCharacterSet")) stop("Mutating immutable character set",selector);
    int intersection=!strcmp(selector,"formIntersectionWithCharacterSet:");
    for (size_t i=0;i<CHARACTER_BYTES;++i) {
        if (intersection) set->bits[i]&=other->bits[i]; else set->bits[i]|=other->bits[i];
    }
}
static Word characters_init(Word object) { Word result=(Word)characters_new(read_word(object)); free((void *)object); return result; }
static Word text_trim(Word object,const char *selector,struct CharacterSet *set) {
    struct TextView text=text_view(object,selector);
    size_t start=0,end=text.length;
    while (start<end) {
        size_t width; unsigned ch=text_scalar(text,start,&width);
        if (!character_member(set,selector,ch)) break;
        start+=width;
    }
    while (end>start) {
        size_t index=end-1;
        if (index>start && text.units[index]>=0xdc00 && text.units[index]<=0xdfff && text.units[index-1]>=0xd800 && text.units[index-1]<=0xdbff) --index;
        size_t width;
        if (!character_member(set,selector,text_scalar(text,index,&width))) break;
        end=index;
    }
    Word result=text_from_units(external_class("_OBJC_CLASS_$_NSString"),text.units+start,end-start);
    free(text.owned);
    return result;
}
static Word text_split_characters(Word object,const char *selector,struct CharacterSet *set) {
    struct TextView text=text_view(object,selector);
    struct Sequence *array=(void *)sequence_empty(external_class("_OBJC_CLASS_$_NSArray"));
    size_t start=0;
    for (size_t index=0;index<text.length;) {
        size_t width; unsigned ch=text_scalar(text,index,&width);
        if (character_member(set,selector,ch)) {
            sequence_add(array,selector,text_from_units(external_class("_OBJC_CLASS_$_NSString"),text.units+start,index-start));
            start=index+width;
        }
        index+=width;
    }
    sequence_add(array,selector,text_from_units(external_class("_OBJC_CLASS_$_NSString"),text.units+start,text.length-start));
    free(text.owned);
    return (Word)array;
}
static struct GuestRange text_find_character(Word object,const char *selector,struct CharacterSet *set) {
    struct TextView text=text_view(object,selector);
    struct GuestRange result=no_range;
    for (size_t index=0;index<text.length;) {
        size_t width; unsigned ch=text_scalar(text,index,&width);
        if (character_member(set,selector,ch)) { result=(struct GuestRange){index,width}; break; }
        index+=width;
    }
    free(text.owned);
    return result;
}
static Method characters_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver) return NULL;
    int mutable=!strcmp(receiver,"_OBJC_CLASS_$_NSMutableCharacterSet");
    if (mutable || !strcmp(receiver,"_OBJC_CLASS_$_NSCharacterSet")) {
        if (class_receiver) {
            for (unsigned i=0;i<COUNT(character_names);++i) if (!strcmp(selector,character_names[i])) return (Method)characters_predefined;
            if (!strcmp(selector,"characterSetWithCharactersInString:")) return (Method)characters_from_string;
        } else {
            if (!strcmp(selector,"init")) return (Method)characters_init;
            if (!strcmp(selector,"characterIsMember:") || !strcmp(selector,"longCharacterIsMember:")) return (Method)character_member;
            if (!strcmp(selector,"copy") || !strcmp(selector,"mutableCopy") || !strcmp(selector,"copyWithZone:") || !strcmp(selector,"mutableCopyWithZone:") || !strcmp(selector,"invertedSet")) return (Method)characters_copy;
            if (mutable) {
                if (!strcmp(selector,"addCharactersInString:") || !strcmp(selector,"removeCharactersInString:")) return (Method)characters_change;
                if (!strcmp(selector,"formIntersectionWithCharacterSet:") || !strcmp(selector,"formUnionWithCharacterSet:")) return (Method)characters_combine;
            }
        }
    }
    if (!class_receiver && (!strcmp(receiver,"_OBJC_CLASS_$_NSString") || !strcmp(receiver,"_OBJC_CLASS_$_NSMutableString"))) {
        if (!strcmp(selector,"stringByTrimmingCharactersInSet:")) return (Method)text_trim;
        if (!strcmp(selector,"componentsSeparatedByCharactersInSet:")) return (Method)text_split_characters;
        if (!strcmp(selector,"rangeOfCharacterFromSet:")) return (Method)text_find_character;
    }
    return NULL;
}
