/* Foundation assertion failures raise the original NSException path. The
 * thread-local handler is static; nothing executes on a healthy/frame path. */
static _Thread_local struct HostObject native_assertion_handler;
static Word assertion_current_handler(Word cls) { native_assertion_handler.isa=cls; return (Word)&native_assertion_handler; }
__attribute__((used,noinline,noreturn)) static void assertion_method_failure(Word self,const char *selector,const char *method,Word object,Word file,intptr_t line,Word description,const unsigned char *arguments) {
    Word message=foundation_format(0,selector,description,arguments);
    char *reason=NULL;
    if (asprintf(&reason,"%s:%ld assertion in %s: %s",file ? string_bytes(file,selector) : "<Unknown File>",(long)line,method ? method : "<Unknown Method>",string_bytes(message,selector))<0) stop("Allocation failed",selector);
    Word text=copy_string(reason); free(reason);
    guest_exception_throw(exception_create(external_class("_OBJC_CLASS_$_NSException"),selector,copy_string("NSInternalInconsistencyException"),text,0));
}
__attribute__((naked)) static void assertion_method_varargs(void) { __asm__ volatile("mov x7,sp\n b assertion_method_failure"); }
__attribute__((used,noinline,noreturn)) static void assertion_function_failure(Word self,const char *selector,Word function,Word file,intptr_t line,Word description,const unsigned char *arguments) {
    Word message=foundation_format(0,selector,description,arguments);
    char *reason=NULL;
    if (asprintf(&reason,"%s:%ld assertion in %s: %s",file ? string_bytes(file,selector) : "<Unknown File>",(long)line,function ? string_bytes(function,selector) : "<Unknown Function>",string_bytes(message,selector))<0) stop("Allocation failed",selector);
    Word text=copy_string(reason); free(reason);
    guest_exception_throw(exception_create(external_class("_OBJC_CLASS_$_NSException"),selector,copy_string("NSInternalInconsistencyException"),text,0));
}
__attribute__((naked)) static void assertion_function_varargs(void) { __asm__ volatile("mov x6,sp\n b assertion_function_failure"); }
static Method assertion_handler_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver || strcmp(receiver,"_OBJC_CLASS_$_NSAssertionHandler")) return NULL;
    if (class_receiver && !strcmp(selector,"currentHandler")) return (Method)assertion_current_handler;
    if (!class_receiver && !strcmp(selector,"handleFailureInMethod:object:file:lineNumber:description:")) return (Method)assertion_method_varargs;
    if (!class_receiver && !strcmp(selector,"handleFailureInFunction:file:lineNumber:description:")) return (Method)assertion_function_varargs;
    return NULL;
}
