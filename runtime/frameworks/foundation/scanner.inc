 
struct Scanner { Word isa,string,skip,locale; size_t position; int case_sensitive; };
static Word scanner_create(Word cls,const char *selector,Word text) {
    if (!text) stop("Nil scanner string",selector);
    struct Scanner *scanner=calloc(1,sizeof(*scanner));
    if (!scanner) stop("Allocation failed",selector);
    scanner->isa=cls; scanner->string=text_copy(text,"copy");
    scanner->skip=characters_predefined(external_class("_OBJC_CLASS_$_NSCharacterSet"),"whitespaceAndNewlineCharacterSet");
    return (Word)scanner;
}
static Word scanner_init(Word object,const char *selector,Word text) { Word result=scanner_create(read_word(object),selector,text); free((void *)object); return result; }
static Word scanner_string(struct Scanner *scanner) { return scanner->string; }
static size_t scanner_start(struct Scanner *scanner) {
    struct TextView text=text_view(scanner->string,"NSScanner");
    size_t position=scanner->position;
    while (position<text.length && scanner->skip) {
        size_t width; unsigned scalar=text_scalar(text,position,&width);
        if (!character_member((void *)scanner->skip,"NSScanner",scalar)) break;
        position+=width;
    }
    free(text.owned); return position;
}
static Word scanner_location(struct Scanner *scanner) { return scanner->position; }
static void scanner_set_location(struct Scanner *scanner,const char *selector,Word position) {
    if (position>string_length(scanner->string,selector)) stop("Scanner position exceeds string",selector);
    scanner->position=position;
}
static Word scanner_at_end(struct Scanner *scanner) { return scanner_start(scanner)==string_length(scanner->string,"isAtEnd"); }
static Word scanner_case(struct Scanner *scanner) { return scanner->case_sensitive; }
static void scanner_set_case(struct Scanner *scanner,const char *selector,unsigned char sensitive) { scanner->case_sensitive=sensitive!=0; }
static Word scanner_skip_set(struct Scanner *scanner) { return scanner->skip; }
static void scanner_set_skip(struct Scanner *scanner,const char *selector,Word set) { scanner->skip=set ? characters_copy((void *)set,"copy") : 0; }
static Word scanner_locale(struct Scanner *scanner) { return scanner->locale; }
static void scanner_set_locale(struct Scanner *scanner,const char *selector,Word locale) { scanner->locale=locale; }
static Word scanner_copy(struct Scanner *scanner) {
    struct Scanner *copy=malloc(sizeof(*copy)); if (!copy) stop("Allocation failed","NSScanner copy"); *copy=*scanner; return (Word)copy;
}
static char *scanner_ascii(struct Scanner *scanner,size_t start) {
    if (scanner->locale) stop("Localized numeric scanner is not implemented","NSScanner");
    struct TextView text=text_view(scanner->string,"NSScanner");
    char *buffer=calloc(text.length-start+1,1);
    if (!buffer) stop("Allocation failed","NSScanner");
    for (size_t i=start;i<text.length && text.units[i]<128;++i) buffer[i-start]=(char)text.units[i];
    free(text.owned); return buffer;
}
static Word scanner_integer(struct Scanner *scanner,const char *selector,void *output) {
    size_t start=scanner_start(scanner);
    char *buffer=scanner_ascii(scanner,start),*end=buffer;
    int hex=strstr(selector,"Hex")!=NULL,wide=strstr(selector,"LongLong")!=NULL || !strcmp(selector,"scanInteger:");
    int unsign=hex || strstr(selector,"Unsigned")!=NULL;
    if (unsign) {
        unsigned long long value=strtoull(buffer,&end,hex ? 16 : 10);
        if (end!=buffer && output) {
            if (wide) *(uint64_t *)output=value;
            else *(uint32_t *)output=(uint32_t)(value>UINT32_MAX ? UINT32_MAX : value);
        }
    } else {
        long long value=strtoll(buffer,&end,10);
        if (end!=buffer && output) {
            if (wide) *(int64_t *)output=value;
            else *(int32_t *)output=(int32_t)(value>INT32_MAX ? INT32_MAX : value<INT32_MIN ? INT32_MIN : value);
        }
    }
    int success=end!=buffer;
    if (success) scanner->position=start+(size_t)(end-buffer);
    free(buffer); return success;
}
static Word scanner_real(struct Scanner *scanner,const char *selector,void *output) {
    size_t start=scanner_start(scanner);
    char *buffer=scanner_ascii(scanner,start),*end=buffer;
    if (strstr(selector,"Hex")) {
        const char *digits=buffer;
        if (*digits=='+' || *digits=='-') ++digits;
        if (digits[0]!='0' || (digits[1]!='x' && digits[1]!='X')) { free(buffer); return 0; }
    }
    double value=strtod(buffer,&end);
    int success=end!=buffer;
    if (success) {
        scanner->position=start+(size_t)(end-buffer);
        if (output) { if (strstr(selector,"Float")) *(float *)output=(float)value; else *(double *)output=value; }
    }
    free(buffer); return success;
}
static Word scanner_scan_string(struct Scanner *scanner,const char *selector,Word needle,Word *output) {
    size_t start=scanner_start(scanner),length=string_length(scanner->string,selector);
    int until=strstr(selector,"UpTo")!=NULL;
    Word options=(scanner->case_sensitive ? 0 : 1)|(until ? 0 : 8);
    struct GuestRange found=text_find_range(scanner->string,selector,needle,options,(struct GuestRange){start,length-start});
    size_t count=until ? (found.location==INT64_MAX ? length-start : found.location-start) : (found.location==INT64_MAX ? 0 : found.length);
    if (!count) return 0;
    if (output) *output=text_subrange(scanner->string,selector,(struct GuestRange){start,count});
    scanner->position=start+count; return 1;
}
static Word scanner_scan_characters(struct Scanner *scanner,const char *selector,struct CharacterSet *set,Word *output) {
    size_t start=scanner_start(scanner),end=start;
    int until=strstr(selector,"UpTo")!=NULL;
    struct TextView text=text_view(scanner->string,selector);
    while (end<text.length) {
        size_t width; unsigned scalar=text_scalar(text,end,&width);
        if ((int)character_member(set,selector,scalar)==until) break;
        end+=width;
    }
    free(text.owned);
    if (end==start) return 0;
    if (output) *output=text_subrange(scanner->string,selector,(struct GuestRange){start,end-start});
    scanner->position=end; return 1;
}
static Method scanner_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver || strcmp(receiver,"_OBJC_CLASS_$_NSScanner")) return NULL;
    if (class_receiver) return !strcmp(selector,"scannerWithString:") ? (Method)scanner_create : NULL;
    if (!strcmp(selector,"initWithString:")) return (Method)scanner_init;
    if (!strcmp(selector,"copy")) return (Method)scanner_copy;
    if (!strcmp(selector,"string")) return (Method)scanner_string;
    if (!strcmp(selector,"scanLocation")) return (Method)scanner_location;
    if (!strcmp(selector,"setScanLocation:")) return (Method)scanner_set_location;
    if (!strcmp(selector,"isAtEnd")) return (Method)scanner_at_end;
    if (!strcmp(selector,"caseSensitive")) return (Method)scanner_case;
    if (!strcmp(selector,"setCaseSensitive:")) return (Method)scanner_set_case;
    if (!strcmp(selector,"charactersToBeSkipped")) return (Method)scanner_skip_set;
    if (!strcmp(selector,"setCharactersToBeSkipped:")) return (Method)scanner_set_skip;
    if (!strcmp(selector,"locale")) return (Method)scanner_locale;
    if (!strcmp(selector,"setLocale:")) return (Method)scanner_set_locale;
    if (!strcmp(selector,"scanInt:") || !strcmp(selector,"scanInteger:") || !strcmp(selector,"scanLongLong:") || !strcmp(selector,"scanUnsignedLongLong:") || !strcmp(selector,"scanHexInt:") || !strcmp(selector,"scanHexLongLong:")) return (Method)scanner_integer;
    if (!strcmp(selector,"scanFloat:") || !strcmp(selector,"scanDouble:") || !strcmp(selector,"scanHexFloat:") || !strcmp(selector,"scanHexDouble:")) return (Method)scanner_real;
    if (!strcmp(selector,"scanString:intoString:") || !strcmp(selector,"scanUpToString:intoString:")) return (Method)scanner_scan_string;
    if (!strcmp(selector,"scanCharactersFromSet:intoString:") || !strcmp(selector,"scanUpToCharactersFromSet:intoString:")) return (Method)scanner_scan_characters;
    return NULL;
}
