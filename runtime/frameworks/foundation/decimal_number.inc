 
static struct HostObject decimal_constants[5];
static void decimal_number_initialize(void) {
    decimal_number_class=external_class("_OBJC_CLASS_$_NSDecimalNumber");
    struct DecimalValue values[]={{0},{1,0,0,0},{(DecimalMantissa)-1,127,0,0},{(DecimalMantissa)-1,127,1,0},{.nan=1}};
    for (unsigned i=0;i<COUNT(values);++i) { decimal_constants[i].isa=decimal_number_class; decimal_store(decimal_constants+i,values[i]); }
}
static Word decimal_number_new(struct DecimalValue value) {
    if (value.nan) return (Word)(decimal_constants+4);
    if (!value.mantissa) return (Word)decimal_constants;
    if (value.mantissa==1 && !value.exponent && !value.negative) return (Word)(decimal_constants+1);
    struct HostObject *number=calloc(1,sizeof(*number)); if (!number) stop("Allocation failed","NSDecimalNumber");
    number->isa=decimal_number_class; decimal_store(number,value); return (Word)number;
}
static Word decimal_constant(Word cls,const char *selector) {
    unsigned i=!strcmp(selector,"one") ? 1 : !strcmp(selector,"maximumDecimalNumber") ? 2 : !strcmp(selector,"minimumDecimalNumber") ? 3 : !strcmp(selector,"notANumber") ? 4 : 0;
    return (Word)(decimal_constants+i);
}
static struct DecimalValue decimal_number_value(struct HostObject *number,const char *selector) {
    if (!number) stop("Nil decimal operand",selector);
    if (number->isa==decimal_number_class) return decimal_load(number);
    if (number->isa!=external_class("_OBJC_CLASS_$_NSNumber")) stop("Expected NSNumber",selector);
    return number->content[1] ? decimal_from_double(numeric_double(number)) : decimal_from_integer(number->content[0],0);
}
static long decimal_compare_numbers(struct HostObject *first,const char *selector,struct HostObject *second) {
    return decimal_compare_values(decimal_number_value(first,selector),decimal_number_value(second,selector));
}
static int decimal_objects_equal(Word first,Word second) {
    if (!first || !second) return 0;
    Word a=read_word(first),b=read_word(second),number=external_class("_OBJC_CLASS_$_NSNumber");
    if ((a!=number && a!=decimal_number_class) || (b!=number && b!=decimal_number_class)) return 0;
    return decimal_compare_numbers((void *)first,"isEqualToNumber:",(void *)second)==0;
}
static Word decimal_equal(struct HostObject *first,const char *selector,struct HostObject *second) { return decimal_objects_equal((Word)first,(Word)second); }
static Word decimal_string(struct HostObject *number,const char *selector) {
    char text[192]; decimal_format(decimal_load(number),text); return copy_string(text);
}
static const char *decimal_type(void) { return "d"; }
static double decimal_get_double(struct HostObject *number) { return decimal_double_value(decimal_load(number)); }
static float decimal_get_float(struct HostObject *number) { return (float)decimal_get_double(number); }
static Word decimal_get_integer(struct HostObject *number,const char *selector) {
    Word value=decimal_integer_value(decimal_load(number));
    if (!strcmp(selector,"intValue")) return (Word)(intptr_t)(int32_t)value;
    if (!strcmp(selector,"unsignedIntValue")) return (uint32_t)value;
    if (!strcmp(selector,"shortValue")) return (Word)(intptr_t)(int16_t)value;
    if (!strcmp(selector,"unsignedShortValue")) return (uint16_t)value;
    if (!strcmp(selector,"charValue")) return (Word)(intptr_t)(int8_t)value;
    if (!strcmp(selector,"unsignedCharValue")) return (uint8_t)value;
    return value;
}
static Word decimal_get_bool(struct HostObject *number) { struct DecimalValue v=decimal_load(number); return v.nan || v.mantissa!=0; }
static Word decimal_init_integer(struct HostObject *object,const char *selector,Word value) {
    if (!strcmp(selector,"initWithInt:")) value=(Word)(intptr_t)(int32_t)value;
    if (!strcmp(selector,"initWithShort:")) value=(Word)(intptr_t)(int16_t)value;
    if (!strcmp(selector,"initWithChar:")) value=(Word)(intptr_t)(int8_t)value;
    if (!strcmp(selector,"initWithUnsignedInt:")) value=(uint32_t)value;
    if (!strcmp(selector,"initWithUnsignedShort:")) value=(uint16_t)value;
    if (!strcmp(selector,"initWithUnsignedChar:")) value=(uint8_t)value;
    decimal_store(object,decimal_from_integer(value,strstr(selector,"Unsigned")!=NULL)); return (Word)object;
}
static Word decimal_init_double(struct HostObject *object,const char *selector,double value) { decimal_store(object,decimal_from_double(value)); return (Word)object; }
static Word decimal_init_float(struct HostObject *object,const char *selector,float value) { return decimal_init_double(object,selector,value); }
static struct DecimalValue decimal_locale_parse(Word input,Word locale,const char *selector) {
    if (!input) return decimal_nan_value();
    Word separator=0;
    if (locale) {
        Word cls=read_word(locale);
        if (cls==external_class("_OBJC_CLASS_$_NSDictionary") || cls==external_class("_OBJC_CLASS_$_NSMutableDictionary"))
            separator=map_get((void *)locale,selector,copy_string("NSDecimalSeparator"));
        else if (cls==external_class("_OBJC_CLASS_$_NSLocale")) separator=locale_component((void *)locale,"decimalSeparator");
        else stop("Unsupported decimal locale",selector);
    }
    return decimal_parse_bytes(string_bytes(input,selector),separator ? string_bytes(separator,selector) : ".");
}
static Word decimal_from_string_locale(Word cls,const char *selector,Word input,Word locale) { return decimal_number_new(decimal_locale_parse(input,locale,selector)); }
static Word decimal_from_string(Word cls,const char *selector,Word input) { return decimal_from_string_locale(cls,selector,input,0); }
static Word decimal_init_string_locale(struct HostObject *object,const char *selector,Word input,Word locale) { decimal_store(object,decimal_locale_parse(input,locale,selector)); return (Word)object; }
static Word decimal_init_string(struct HostObject *object,const char *selector,Word input) { return decimal_init_string_locale(object,selector,input,0); }
static Word decimal_from_mantissa(Word cls,const char *selector,uint64_t mantissa,int16_t exponent,unsigned char negative) {
    struct DecimalValue value={mantissa,exponent,negative!=0,0};
    if (decimal_normalize(&value)) value=decimal_nan_value(); return decimal_number_new(value);
}
static Word decimal_init_mantissa(struct HostObject *object,const char *selector,uint64_t mantissa,int16_t exponent,unsigned char negative) {
    struct DecimalValue value={mantissa,exponent,negative!=0,0};
    if (decimal_normalize(&value)) value=decimal_nan_value(); decimal_store(object,value); return (Word)object;
}
static Word decimal_result(struct DecimalValue value,int error,const char *selector) {
    if (error) {
        const char *name=error==1 ? "NSDecimalNumberOverflowException" : error==2 ? "NSDecimalNumberUnderflowException" : "NSDecimalNumberDivideByZeroException";
        guest_exception_throw(exception_create(external_class("_OBJC_CLASS_$_NSException"),selector,copy_string(name),copy_string(selector),0));
    }
    return decimal_number_new(value);
}
static Word decimal_multiply(struct HostObject *self,const char *selector,struct HostObject *other) {
    int error=0; struct DecimalValue result=decimal_multiply_values(decimal_load(self),decimal_number_value(other,selector),&error);
    return decimal_result(result,error,selector);
}
static Word decimal_divide(struct HostObject *self,const char *selector,struct HostObject *other) {
    int error=0; struct DecimalValue result=decimal_divide_values(decimal_load(self),decimal_number_value(other,selector),&error);
    return decimal_result(result,error,selector);
}
static Word decimal_power10(struct HostObject *self,const char *selector,int16_t power) {
    struct DecimalValue result=decimal_load(self); result.exponent+=power;
    return decimal_result(result,decimal_normalize(&result),selector);
}
static Method decimal_number_method(Word object,const char *selector,const char *receiver,int class_receiver) {
     
    if ((class_receiver ? object : read_word(object))!=decimal_number_class) return NULL;
    if (class_receiver) {
        if (!strcmp(selector,"alloc") || !strcmp(selector,"allocWithZone:")) return (Method)host_allocate;
        if (!strcmp(selector,"zero") || !strcmp(selector,"one") || !strcmp(selector,"maximumDecimalNumber") || !strcmp(selector,"minimumDecimalNumber") || !strcmp(selector,"notANumber")) return (Method)decimal_constant;
        if (!strcmp(selector,"decimalNumberWithString:")) return (Method)decimal_from_string;
        if (!strcmp(selector,"decimalNumberWithString:locale:")) return (Method)decimal_from_string_locale;
        if (!strcmp(selector,"decimalNumberWithMantissa:exponent:isNegative:")) return (Method)decimal_from_mantissa;
    } else {
        if (!strcmp(selector,"init") || !strcmp(selector,"copy") || !strcmp(selector,"copyWithZone:")) return (Method)identity;
        if (!strcmp(selector,"initWithInteger:") || !strcmp(selector,"initWithLongLong:") || !strcmp(selector,"initWithLong:") || !strcmp(selector,"initWithInt:") || !strcmp(selector,"initWithShort:") || !strcmp(selector,"initWithChar:") || !strcmp(selector,"initWithUnsignedInteger:") || !strcmp(selector,"initWithUnsignedLongLong:") || !strcmp(selector,"initWithUnsignedLong:") || !strcmp(selector,"initWithUnsignedInt:") || !strcmp(selector,"initWithUnsignedShort:") || !strcmp(selector,"initWithUnsignedChar:")) return (Method)decimal_init_integer;
        if (!strcmp(selector,"initWithDouble:")) return (Method)decimal_init_double;
        if (!strcmp(selector,"initWithFloat:")) return (Method)decimal_init_float;
        if (!strcmp(selector,"initWithString:")) return (Method)decimal_init_string;
        if (!strcmp(selector,"initWithString:locale:")) return (Method)decimal_init_string_locale;
        if (!strcmp(selector,"initWithMantissa:exponent:isNegative:")) return (Method)decimal_init_mantissa;
        if (!strcmp(selector,"compare:")) return (Method)decimal_compare_numbers;
        if (!strcmp(selector,"isEqual:") || !strcmp(selector,"isEqualToNumber:")) return (Method)decimal_equal;
        if (!strcmp(selector,"stringValue") || !strcmp(selector,"description")) return (Method)decimal_string;
        if (!strcmp(selector,"doubleValue")) return (Method)decimal_get_double;
        if (!strcmp(selector,"floatValue")) return (Method)decimal_get_float;
        if (!strcmp(selector,"boolValue")) return (Method)decimal_get_bool;
        if (!strcmp(selector,"objCType")) return (Method)decimal_type;
        if (!strcmp(selector,"integerValue") || !strcmp(selector,"intValue") || !strcmp(selector,"longValue") || !strcmp(selector,"longLongValue") || !strcmp(selector,"shortValue") || !strcmp(selector,"charValue") || !strcmp(selector,"unsignedIntegerValue") || !strcmp(selector,"unsignedIntValue") || !strcmp(selector,"unsignedLongValue") || !strcmp(selector,"unsignedLongLongValue") || !strcmp(selector,"unsignedShortValue") || !strcmp(selector,"unsignedCharValue")) return (Method)decimal_get_integer;
        if (!strcmp(selector,"decimalNumberByMultiplyingBy:")) return (Method)decimal_multiply;
        if (!strcmp(selector,"decimalNumberByDividingBy:")) return (Method)decimal_divide;
        if (!strcmp(selector,"decimalNumberByMultiplyingByPowerOf10:")) return (Method)decimal_power10;
    }
    return NULL;
}
