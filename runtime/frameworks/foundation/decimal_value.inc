 
typedef unsigned __int128 DecimalMantissa;
struct DecimalValue { DecimalMantissa mantissa; int exponent; unsigned negative,nan; };
static Word decimal_number_class;
static int decimal_objects_equal(Word,Word);
static long decimal_compare_numbers(struct HostObject *,const char *,struct HostObject *);
static struct DecimalValue decimal_nan_value(void) { return (struct DecimalValue){.nan=1}; }
static struct DecimalValue decimal_load(const struct HostObject *object) {
    return (struct DecimalValue){(DecimalMantissa)object->content[0]|((DecimalMantissa)object->content[1]<<64),
        (int)(int32_t)object->content[2],(unsigned)(object->content[3]&1),(unsigned)((object->content[3]>>1)&1)};
}
static void decimal_store(struct HostObject *object,struct DecimalValue value) {
    object->content[0]=(Word)value.mantissa; object->content[1]=(Word)(value.mantissa>>64);
    object->content[2]=(Word)(intptr_t)value.exponent; object->content[3]=value.negative|(value.nan<<1);
}
 
static int decimal_normalize(struct DecimalValue *value) {
    if (value->nan) return 0;
    if (!value->mantissa) { value->exponent=0; value->negative=0; return 0; }
    while (value->exponent<127 && value->mantissa%10==0) { value->mantissa/=10; ++value->exponent; }
    while (value->exponent>127 && value->mantissa<=(DecimalMantissa)-1/10) { value->mantissa*=10; --value->exponent; }
    return value->exponent>127 ? 1 : value->exponent<-128 ? 2 : 0;
}
static unsigned decimal_digits(DecimalMantissa value,unsigned char *digits) {
    unsigned n=0;
    do { digits[n++]=(unsigned char)(value%10); value/=10; } while (value);
    for (unsigned i=0;i<n/2;++i) { unsigned char t=digits[i]; digits[i]=digits[n-1-i]; digits[n-1-i]=t; }
    return n;
}
 
static struct DecimalValue decimal_fit(const unsigned char *digits,unsigned n,int exponent,unsigned negative,int *error) {
    DecimalMantissa mantissa=0,maximum=(DecimalMantissa)-1;
    unsigned used=0;
    while (used<n && mantissa<=(maximum-digits[used])/10) mantissa=mantissa*10+digits[used++];
    if (used<n && digits[used]>=5) {
        if (mantissa==maximum) { mantissa/=10; --used; }
        if (digits[used]>=5) ++mantissa;
    }
    struct DecimalValue result={mantissa,exponent+(int)(n-used),negative,0};
    *error=decimal_normalize(&result); return result;
}
static struct DecimalValue decimal_parse_bytes(const char *text,const char *separator) {
    if (!text || !separator || !*separator) return decimal_nan_value();
    while (*text==' ' || *text=='\t' || *text=='\n' || *text=='\r') ++text;
    unsigned negative=*text=='-'; if (*text=='-' || *text=='+') ++text;
    unsigned char digits[80]; unsigned stored=0; size_t significant=0,fraction=0;
    size_t separator_length=strlen(separator); int point=0,any=0,started=0;
    for (;;) {
        if (*text>='0' && *text<='9') {
            unsigned digit=(unsigned)(*text++-'0'); any=1; if (point) ++fraction;
            if (digit || started) { started=1; ++significant; if (stored<COUNT(digits)) digits[stored++]=(unsigned char)digit; }
        } else if (!point && !strncmp(text,separator,separator_length)) { point=1; text+=separator_length; }
        else break;
    }
    int exponent=0;
    if (*text=='e' || *text=='E') {
        ++text; int minus=*text=='-'; if (*text=='-' || *text=='+') ++text;
        if (*text<'0' || *text>'9') return decimal_nan_value();
        do { if (exponent>100000) return decimal_nan_value(); exponent=exponent*10+(*text++-'0'); } while (*text>='0' && *text<='9');
        if (minus) exponent=-exponent;
    }
    while (*text==' ' || *text=='\t' || *text=='\n' || *text=='\r') ++text;
    if (*text || !any || fraction>INT_MAX || significant>INT_MAX) return decimal_nan_value();
    if (!started) return (struct DecimalValue){0};
    int64_t power=(int64_t)exponent-(int64_t)fraction+(int64_t)significant-stored;
    if (power<INT_MIN+80LL || power>INT_MAX-80LL) return decimal_nan_value();
    int error=0; struct DecimalValue result=decimal_fit(digits,stored,(int)power,negative,&error);
    return error ? decimal_nan_value() : result;
}
static struct DecimalValue decimal_from_integer(Word value,unsigned is_unsigned) {
    unsigned negative=!is_unsigned && (intptr_t)value<0;
    struct DecimalValue result={negative ? (Word)0-value : value,0,negative,0};
    decimal_normalize(&result); return result;
}
static struct DecimalValue decimal_from_double(double value) {
    if (!isfinite(value)) return decimal_nan_value();
    double magnitude=fabs(value);
     
    if (magnitude<0x1p128 && floor(magnitude)==magnitude) {
        struct DecimalValue result={(DecimalMantissa)magnitude,0,value<0,0};
        decimal_normalize(&result); return result;
    }
    char text[48]; snprintf(text,sizeof(text),"%.17g",value); return decimal_parse_bytes(text,".");
}
static long decimal_compare_values(struct DecimalValue a,struct DecimalValue b) {
    if (a.nan || b.nan) return a.nan ? (b.nan ? 0 : -1) : 1;
    if (!a.mantissa) a.negative=0;
    if (!b.mantissa) b.negative=0;
    if (a.negative!=b.negative) return a.negative ? -1 : 1;
    long sign=a.negative ? -1 : 1;
    if (!a.mantissa || !b.mantissa) return a.mantissa ? sign : b.mantissa ? -sign : 0;
    unsigned char ad[40],bd[40]; unsigned an=decimal_digits(a.mantissa,ad),bn=decimal_digits(b.mantissa,bd);
    int ao=(int)an+a.exponent,bo=(int)bn+b.exponent;
    if (ao!=bo) return ao<bo ? -sign : sign;
    for (unsigned i=0;i<an || i<bn;++i) {
        unsigned x=i<an ? ad[i] : 0,y=i<bn ? bd[i] : 0;
        if (x!=y) return x<y ? -sign : sign;
    }
    return 0;
}
static double decimal_double_value(struct DecimalValue value) {
    if (value.nan) return NAN;
    double result=(double)value.mantissa;
    if (value.exponent) result*=pow(10.0,value.exponent);
    return value.negative ? -result : result;
}
static Word decimal_integer_value(struct DecimalValue value) {
    if (value.nan) return 0;
    DecimalMantissa mantissa=value.mantissa;
    if (value.exponent<0) {
        for (int i=0;i>value.exponent && mantissa;--i) mantissa/=10;
        value.exponent=0;
    }
    Word result=(Word)mantissa;
    for (int i=0;i<value.exponent;++i) result*=10;
    return value.negative ? (Word)0-result : result;
}
static void decimal_format(struct DecimalValue value,char *text) {
    if (value.nan) { strcpy(text,"NaN"); return; }
    if (!value.mantissa) { strcpy(text,"0"); return; }
    unsigned char digits[40]; unsigned n=decimal_digits(value.mantissa,digits); char *out=text;
    if (value.negative) *out++='-';
    int point=(int)n+value.exponent;
    if (point<=0) { *out++='0'; *out++='.'; for (int i=0;i>point;--i) *out++='0'; }
    for (unsigned i=0;i<n;++i) { if (point>0 && (int)i==point) *out++='.'; *out++=(char)('0'+digits[i]); }
    for (int i=(int)n;i<point;++i) *out++='0';
    *out=0;  
}
static struct DecimalValue decimal_multiply_values(struct DecimalValue a,struct DecimalValue b,int *error) {
    *error=0;
    if (a.nan || b.nan) return decimal_nan_value();
    struct DecimalValue result={0,a.exponent+b.exponent,a.negative^b.negative,0};
    if (!a.mantissa || !b.mantissa) return (struct DecimalValue){0};
     
    if (a.mantissa==1 || b.mantissa==1) {
        result.mantissa=a.mantissa==1 ? b.mantissa : a.mantissa;
        *error=decimal_normalize(&result); return result;
    }
    if (a.mantissa<=(DecimalMantissa)-1/b.mantissa) {
        result.mantissa=a.mantissa*b.mantissa; *error=decimal_normalize(&result); return result;
    }
    unsigned char ad[40],bd[40],digits[80]; uint16_t product[80]={0};
    unsigned an=decimal_digits(a.mantissa,ad),bn=decimal_digits(b.mantissa,bd),n=an+bn;
    for (unsigned i=0;i<an;++i) for (unsigned j=0;j<bn;++j) product[i+j]+=(uint16_t)(ad[an-1-i]*bd[bn-1-j]);
    for (unsigned i=0;i+1<n;++i) { product[i+1]+=product[i]/10; product[i]%=10; }
    while (n>1 && !product[n-1]) --n;
    for (unsigned i=0;i<n;++i) digits[i]=(unsigned char)product[n-1-i];
    return decimal_fit(digits,n,result.exponent,result.negative,error);
}
 
static unsigned decimal_division_digit(DecimalMantissa *remainder,DecimalMantissa divisor) {
    DecimalMantissa low=(DecimalMantissa)(uint64_t)*remainder*10;
    DecimalMantissa high=(DecimalMantissa)(uint64_t)(*remainder>>64)*10+(low>>64);
    DecimalMantissa value=(uint64_t)low|((DecimalMantissa)(uint64_t)high<<64);
    unsigned carry=(unsigned)(high>>64),digit=0;
    while (carry || value>=divisor) { DecimalMantissa old=value; value-=divisor; if (old<divisor) --carry; ++digit; }
    *remainder=value; return digit;
}
static struct DecimalValue decimal_divide_values(struct DecimalValue a,struct DecimalValue b,int *error) {
    *error=0;
    if (a.nan || b.nan) return decimal_nan_value();
    if (!b.mantissa) { *error=3; return decimal_nan_value(); }
    if (!a.mantissa) return (struct DecimalValue){0};
    struct DecimalValue result={a.mantissa,a.exponent-b.exponent,a.negative^b.negative,0};
    if (b.mantissa==1) { *error=decimal_normalize(&result); return result; }
    DecimalMantissa quotient=a.mantissa/b.mantissa,remainder=a.mantissa%b.mantissa,maximum=(DecimalMantissa)-1;
    while (remainder) {
        unsigned digit=decimal_division_digit(&remainder,b.mantissa);
        if (quotient>(maximum-digit)/10) {
            if (digit>=5) {
                if (quotient==maximum) { unsigned last=(unsigned)(quotient%10); quotient=quotient/10+(last>=5); ++result.exponent; }
                else ++quotient;
            }
            break;
        }
        quotient=quotient*10+digit; --result.exponent;
    }
    result.mantissa=quotient; *error=decimal_normalize(&result); return result;
}
