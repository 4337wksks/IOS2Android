static struct HostObject game_center_player,payment_queue;
static Word payment_observers;
struct StorePayment { Word isa,identifier,username; intptr_t quantity; };
struct StoreTransaction { Word isa,payment,error; unsigned state,active,announced; };
static struct Sequence *payment_transactions;
static pthread_mutex_t store_payment_mutex=PTHREAD_MUTEX_INITIALIZER;
struct StoreQueueDelivery { Word observer; struct Sequence *transactions; unsigned removed; };
static struct StoreQueueDelivery *store_replay_observer(Word);
static void store_queue_deliver(void *);
static Word unsupported_service_error(const char *domain,int code,const char *message) {
    struct GuestMap *info=(void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));
    map_set(info,"Platform service",copy_string(message),copy_string("NSLocalizedDescription"));
    return error_with_domain(external_class("_OBJC_CLASS_$_NSError"),"Platform service",copy_string(domain),code,(Word)info);
}
static Word game_center_local_player(Word cls) { game_center_player.isa=cls; return (Word)&game_center_player; }
static Word service_unavailable(void) { return 0; }
static void game_center_authenticate_callback(void *context) {
    struct Block_layout *block=context;
    Word error=unsupported_service_error("GKErrorDomain",16,"Game Center is unavailable on Android");
    ((void (*)(void *,Word,Word))block->invoke)(block,0,error);
    _Block_release(block);
}
static void game_center_authenticate(struct HostObject *player,const char *selector,Word block) {
    player->content[0]=block ? (Word)_Block_copy((void *)block) : 0;
    if (block) schedule_main_function(game_center_authenticate_callback,_Block_copy((void *)block));
}
static Word game_center_authentication_handler(struct HostObject *player) { return player->content[0]; }
static Word store_queue(Word cls) {
    pthread_mutex_lock(&store_payment_mutex);
    payment_queue.isa=cls;
    if (!payment_observers) payment_observers=sequence_empty(external_class("_OBJC_CLASS_$_NSMutableSet"));
    pthread_mutex_unlock(&store_payment_mutex);
    return (Word)&payment_queue;
}
static void store_add_observer(Word self,const char *selector,Word observer) {
    if (!observer) stop("Nil transaction observer",selector);
    store_queue(external_class("_OBJC_CLASS_$_SKPaymentQueue"));
    pthread_mutex_lock(&store_payment_mutex);
    int added=!sequence_contains((void *)payment_observers,selector,observer);
    if (added) metal_object_retain(observer);
    sequence_add((void *)payment_observers,selector,observer);
    struct StoreQueueDelivery *replay=added ? store_replay_observer(observer) : NULL;
    pthread_mutex_unlock(&store_payment_mutex);
    if (replay) schedule_main_function(store_queue_deliver,replay);
}
static void store_remove_observer(Word self,const char *selector,Word observer) {
    pthread_mutex_lock(&store_payment_mutex);
    int removed=payment_observers && sequence_contains((void *)payment_observers,selector,observer);
    if (removed) sequence_remove((void *)payment_observers,selector,observer);
    pthread_mutex_unlock(&store_payment_mutex);
    if (removed) metal_object_release(observer);
}
static Word store_transactions(void) {
    pthread_mutex_lock(&store_payment_mutex);
    Word result=payment_transactions ? sequence_copy(payment_transactions,"copy") : sequence_empty(external_class("_OBJC_CLASS_$_NSArray"));
    pthread_mutex_unlock(&store_payment_mutex); return result;
}
static Word store_payment_initialize(Word object) { ((struct StorePayment *)object)->quantity=1; return object; }
static Word store_payment_create(Word cls,const char *selector,Word product) {
    if (!product) stop("Nil StoreKit product",selector);
    Method identifier=find_implementation(product,"productIdentifier");
    if (!identifier) stop("Invalid StoreKit product",selector);
    Word value=identifier(product,"productIdentifier",0,0,0,0);
    if (!value || !*string_bytes(value,selector)) stop("Empty product identifier",selector);
    struct StorePayment *payment=(void *)host_allocate(cls);
    payment->identifier=text_copy(value,"copy"); payment->quantity=1; return (Word)payment;
}
static Word store_payment_copy(Word object,const char *selector,Word zone) {
    struct StorePayment *source=(void *)object;
    Word cls=external_class(!strncmp(selector,"mutableCopy",11) ? "_OBJC_CLASS_$_SKMutablePayment" : "_OBJC_CLASS_$_SKPayment");
    struct StorePayment *copy=(void *)host_allocate(cls);
    copy->identifier=source->identifier; copy->username=source->username; copy->quantity=source->quantity; return (Word)copy;
}
static Word store_payment_property(struct StorePayment *payment,const char *selector) {
    if (!strcmp(selector,"productIdentifier")) return payment->identifier;
    if (!strcmp(selector,"applicationUsername")) return payment->username;
    return payment->quantity;
}
static void store_payment_set_property(struct StorePayment *payment,const char *selector,Word value) {
    if (!strcmp(selector,"setQuantity:")) {
        if ((intptr_t)value<1) stop("Invalid payment quantity",selector);
        payment->quantity=value;
    } else if (!strcmp(selector,"setProductIdentifier:")) {
        if (!value || !*string_bytes(value,selector)) stop("Empty product identifier",selector);
        payment->identifier=text_copy(value,"copy");
    } else payment->username=value ? text_copy(value,"copy") : 0;
}
static Word store_transaction_property(struct StoreTransaction *transaction,const char *selector) {
    if (!strcmp(selector,"transactionState")) return transaction->state;
    if (!strcmp(selector,"payment")) return transaction->payment;
    if (!strcmp(selector,"error")) return transaction->state==2 ? transaction->error : 0;
    return 0;
}
static void store_queue_deliver(void *context) {
    struct StoreQueueDelivery *delivery=context;
    const char *selector=delivery->removed ? "paymentQueue:removedTransactions:" : "paymentQueue:updatedTransactions:";
    pthread_mutex_lock(&store_payment_mutex);
    struct Sequence *transactions=(void *)sequence_empty(external_class("_OBJC_CLASS_$_NSArray"));
    for (size_t i=0;i<delivery->transactions->count;++i) {
        struct StoreTransaction *transaction=(void *)delivery->transactions->items[i];
        if (delivery->removed || transaction->active) {
            if (!delivery->removed && !delivery->observer) { transaction->state=2; transaction->announced=1; }
            sequence_add(transactions,selector,(Word)transaction);
        }
    }
    struct Sequence *observers=(void *)sequence_copy((void *)payment_observers,"copy");
    for (size_t i=0;i<observers->count;++i) metal_object_retain(observers->items[i]);
    pthread_mutex_unlock(&store_payment_mutex);
    for (size_t i=0;transactions->count && i<observers->count;++i) {
        Word observer=observers->items[i];
        if (delivery->observer && observer!=delivery->observer) continue;
        pthread_mutex_lock(&store_payment_mutex);
        int active=sequence_contains((void *)payment_observers,selector,observer);
        pthread_mutex_unlock(&store_payment_mutex);
        if (!active) continue;
        Method callback=find_implementation(observer,selector);
        if (callback) callback(observer,selector,(Word)&payment_queue,(Word)transactions,0,0);
    }
    for (size_t i=0;i<observers->count;++i) metal_object_release(observers->items[i]);
    free(observers->items); free(observers);
    if (!transactions->count) { free(transactions->items); free(transactions); }
    free(delivery->transactions->items); free(delivery->transactions); free(delivery);
}
static struct StoreQueueDelivery *store_replay_observer(Word observer) {
    struct StoreQueueDelivery *delivery=calloc(1,sizeof(*delivery)); if (!delivery) stop("Allocation failed","SKPaymentQueue");
    delivery->observer=observer; delivery->transactions=(void *)sequence_empty(external_class("_OBJC_CLASS_$_NSArray"));
    for (size_t i=0;payment_transactions && i<payment_transactions->count;++i) {
        struct StoreTransaction *transaction=(void *)payment_transactions->items[i];
        if (transaction->announced) sequence_add(delivery->transactions,"SKPaymentQueue",(Word)transaction);
    }
    if (delivery->transactions->count) return delivery;
    free(delivery->transactions); free(delivery); return NULL;
}
static void store_add_payment(Word self,const char *selector,Word object) {
    if (!object || !class_inherits(read_word(object),external_class("_OBJC_CLASS_$_SKPayment"))) stop("Invalid StoreKit payment",selector);
    struct StorePayment *payment=(void *)object;
    if (!payment->identifier || !*string_bytes(payment->identifier,selector) || payment->quantity<1) stop("Invalid StoreKit payment",selector);
    store_queue(external_class("_OBJC_CLASS_$_SKPaymentQueue"));
    struct StoreTransaction *transaction=(void *)host_allocate(external_class("_OBJC_CLASS_$_SKPaymentTransaction"));
    transaction->payment=store_payment_copy(object,"copy",0); transaction->active=1;
    transaction->error=unsupported_service_error("SKErrorDomain",4,"StoreKit payments are unavailable on this host");
    struct StoreQueueDelivery *delivery=calloc(1,sizeof(*delivery)); if (!delivery) stop("Allocation failed",selector);
    Word item=(Word)transaction; delivery->transactions=(void *)new_array(external_class("_OBJC_CLASS_$_NSArray"),selector,&item,1);
    pthread_mutex_lock(&store_payment_mutex);
    if (!payment_transactions) payment_transactions=(void *)sequence_empty(external_class("_OBJC_CLASS_$_NSMutableArray"));
    sequence_add(payment_transactions,selector,item);
    pthread_mutex_unlock(&store_payment_mutex);
    schedule_main_function(store_queue_deliver,delivery);
}
static void store_finish_transaction(Word self,const char *selector,Word object) {
    if (!object || !class_inherits(read_word(object),external_class("_OBJC_CLASS_$_SKPaymentTransaction"))) stop("Invalid StoreKit transaction",selector);
    struct StoreTransaction *transaction=(void *)object;
    pthread_mutex_lock(&store_payment_mutex);
    if (!transaction->active) { pthread_mutex_unlock(&store_payment_mutex); return; }
    if (transaction->state!=2) { pthread_mutex_unlock(&store_payment_mutex); stop("Cannot finish a pending transaction",selector); }
    transaction->active=0; sequence_remove(payment_transactions,selector,object);
    pthread_mutex_unlock(&store_payment_mutex);
    struct StoreQueueDelivery *delivery=calloc(1,sizeof(*delivery)); if (!delivery) stop("Allocation failed",selector);
    delivery->removed=1; delivery->transactions=(void *)new_array(external_class("_OBJC_CLASS_$_NSArray"),selector,&object,1);
    schedule_main_function(store_queue_deliver,delivery);
}
struct StoreProductsRequestState { Word object,identifiers,delegate; unsigned generation,running; struct StoreProductsRequestState *next; };
struct StoreProductsDelivery { struct StoreProductsRequestState *request; unsigned generation; };
static struct StoreProductsRequestState *store_product_requests;
static pthread_mutex_t store_product_mutex=PTHREAD_MUTEX_INITIALIZER;
static struct StoreProductsRequestState *store_product_state(Word object) {
    for (struct StoreProductsRequestState *state=store_product_requests;state;state=state->next) if (state->object==object) return state;
    struct StoreProductsRequestState *state=calloc(1,sizeof(*state));
    if (!state) stop("Allocation failed","SKProductsRequest");
    state->object=object; state->next=store_product_requests; store_product_requests=state; return state;
}
static Word store_products_init(Word object,const char *selector,Word identifiers) {
    if (!identifiers || !sequence_set(read_word(identifiers))) stop("Expected product identifier set",selector);
    pthread_mutex_lock(&store_product_mutex);
    struct StoreProductsRequestState *state=store_product_state(object);
    state->identifiers=sequence_copy((void *)identifiers,"copy"); state->running=0; ++state->generation;
    pthread_mutex_unlock(&store_product_mutex); return object;
}
static void store_products_delegate(Word object,const char *selector,Word delegate) {
    pthread_mutex_lock(&store_product_mutex); store_product_state(object)->delegate=delegate; pthread_mutex_unlock(&store_product_mutex);
}
static Word store_products_get_delegate(Word object) {
    pthread_mutex_lock(&store_product_mutex); Word delegate=store_product_state(object)->delegate; pthread_mutex_unlock(&store_product_mutex); return delegate;
}
static Word store_response_products(struct HostObject *response) { return response->content[0]; }
static Word store_response_invalid(struct HostObject *response) { return response->content[1]; }
static Word store_empty_products(Word identifiers) {
    struct HostObject *response=(void *)host_allocate(external_class("_OBJC_CLASS_$_SKProductsResponse"));
    response->content[0]=sequence_empty(external_class("_OBJC_CLASS_$_NSArray"));
    response->content[1]=identifiers ? set_all((void *)identifiers,"allObjects") : sequence_empty(external_class("_OBJC_CLASS_$_NSArray"));
    return (Word)response;
}
static void store_products_deliver(void *context) {
    struct StoreProductsDelivery *delivery=context; struct StoreProductsRequestState *state=delivery->request;
    unsigned generation=delivery->generation;
    pthread_mutex_lock(&store_product_mutex);
    int active=state->running && state->generation==delivery->generation;
    Word delegate=active ? state->delegate : 0,identifiers=state->identifiers;
    if (active) state->running=0;
    pthread_mutex_unlock(&store_product_mutex); free(delivery);
    if (!delegate) return;
    Word response=runtime_extension.unavailable_products_response ? runtime_extension.unavailable_products_response(identifiers) :
        STOREKIT_EMPTY_CATALOGUE ? store_empty_products(identifiers) : 0;
    Method callback;
    if (!response) {
        Word error=unsupported_service_error("SKErrorDomain",0,"StoreKit catalogue is unavailable on this host");
        callback=find_implementation(delegate,"request:didFailWithError:");
        if (callback) callback(delegate,"request:didFailWithError:",state->object,error,0,0);
        return;
    }
    callback=find_implementation(delegate,"productsRequest:didReceiveResponse:");
    if (callback) callback(delegate,"productsRequest:didReceiveResponse:",state->object,response,0,0);
    pthread_mutex_lock(&store_product_mutex);
    delegate=state->generation==generation ? state->delegate : 0;
    pthread_mutex_unlock(&store_product_mutex);
    callback=delegate ? find_implementation(delegate,"requestDidFinish:") : NULL;
    if (callback) callback(delegate,"requestDidFinish:",state->object,0,0,0);
}
static void store_products_start(Word object,const char *selector) {
    struct StoreProductsDelivery *delivery=calloc(1,sizeof(*delivery)); if (!delivery) stop("Allocation failed",selector);
    pthread_mutex_lock(&store_product_mutex); struct StoreProductsRequestState *state=store_product_state(object);
    if (!state->identifiers) { pthread_mutex_unlock(&store_product_mutex); free(delivery); stop("Uninitialized products request",selector); }
    if (state->running) { pthread_mutex_unlock(&store_product_mutex); free(delivery); return; }
    state->running=1; delivery->request=state; delivery->generation=++state->generation; pthread_mutex_unlock(&store_product_mutex);
    schedule_main_function(store_products_deliver,delivery);
}
static void store_products_cancel(Word object) {
    pthread_mutex_lock(&store_product_mutex); struct StoreProductsRequestState *state=store_product_state(object);
    state->running=0; ++state->generation; pthread_mutex_unlock(&store_product_mutex);
}
static void guest_notification_haptic(Word object,const char *selector,Word kind) {
    if (kind>2) stop("Invalid notification feedback type",selector);
    JNIEnv *env=attached_environment(selector); jclass cls=(*env)->GetObjectClass(env,activity);
    jmethodID haptic=(*env)->GetMethodID(env,cls,"guestHaptic","(I)V"); (*env)->CallVoidMethod(env,activity,haptic,(jint)kind); (*env)->DeleteLocalRef(env,cls);
}
static void guest_simple_haptic(Word object,const char *selector) { guest_notification_haptic(object,selector,0); }
static Word guest_impact_init(struct HostObject *object,const char *selector,Word style) {
    if (style>4) stop("Invalid impact feedback style",selector);
    object->content[0]=style; return (Word)object;
}
static Word unavailable_call_center(Word object) {
     
    LOG("CTCallCenter unavailable: this Android package has no authorized call-state observer");
    free((void *)object); return 0;
}
static Method apple_services_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver) return NULL;
    const char *base=class_receiver ? receiver : external_base(read_word(object));
    Word cls=class_receiver ? object : read_word(object);
    if (class_inherits(cls,external_class("_OBJC_CLASS_$_SKPayment"))) {
        if (class_receiver && !strcmp(selector,"paymentWithProduct:")) return (Method)store_payment_create;
        if (!class_receiver) {
            if (!strcmp(selector,"init")) return (Method)store_payment_initialize;
            if (!strcmp(selector,"productIdentifier") || !strcmp(selector,"quantity") || !strcmp(selector,"applicationUsername")) return (Method)store_payment_property;
            if (!strcmp(selector,"copy") || !strcmp(selector,"copyWithZone:") || !strcmp(selector,"mutableCopy") || !strcmp(selector,"mutableCopyWithZone:")) return (Method)store_payment_copy;
            if (class_inherits(cls,external_class("_OBJC_CLASS_$_SKMutablePayment")) &&
                (!strcmp(selector,"setProductIdentifier:") || !strcmp(selector,"setQuantity:") || !strcmp(selector,"setApplicationUsername:"))) return (Method)store_payment_set_property;
        }
    }
    if (!class_receiver && class_inherits(cls,external_class("_OBJC_CLASS_$_SKPaymentTransaction")) &&
        (!strcmp(selector,"transactionState") || !strcmp(selector,"payment") || !strcmp(selector,"error") || !strcmp(selector,"transactionIdentifier") ||
         !strcmp(selector,"transactionDate") || !strcmp(selector,"originalTransaction"))) return (Method)store_transaction_property;
    if (!class_receiver && base && !strcmp(base,"_OBJC_CLASS_$_SKProductsRequest")) {
        if (!strcmp(selector,"initWithProductIdentifiers:")) return (Method)store_products_init;
        if (!strcmp(selector,"setDelegate:")) return (Method)store_products_delegate;
        if (!strcmp(selector,"delegate")) return (Method)store_products_get_delegate;
        if (!strcmp(selector,"start")) return (Method)store_products_start;
        if (!strcmp(selector,"cancel")) return (Method)store_products_cancel;
    }
    if (!class_receiver && !strcmp(receiver,"_OBJC_CLASS_$_SKProductsResponse")) {
        if (!strcmp(selector,"products")) return (Method)store_response_products;
        if (!strcmp(selector,"invalidProductIdentifiers")) return (Method)store_response_invalid;
    }
    if (!class_receiver && (!strcmp(receiver,"_OBJC_CLASS_$_UINotificationFeedbackGenerator") ||
        !strcmp(receiver,"_OBJC_CLASS_$_UIImpactFeedbackGenerator") || !strcmp(receiver,"_OBJC_CLASS_$_UISelectionFeedbackGenerator"))) {
        if (!strcmp(selector,"notificationOccurred:")) return (Method)guest_notification_haptic;
        if (!strcmp(selector,"initWithStyle:")) return (Method)guest_impact_init;
        if (!strcmp(selector,"impactOccurred") || !strcmp(selector,"selectionChanged")) return (Method)guest_simple_haptic;
        if (!strcmp(selector,"prepare")) return (Method)noop;
    }
    if (!class_receiver && !strcmp(receiver,"_OBJC_CLASS_$_CTCallCenter") && !strcmp(selector,"init")) return (Method)unavailable_call_center;
    if (!strcmp(receiver,"_OBJC_CLASS_$_GKLocalPlayer")) {
        if (class_receiver && !strcmp(selector,"localPlayer")) return (Method)game_center_local_player;
        if (!class_receiver) {
            if (!strcmp(selector,"setAuthenticateHandler:")) return (Method)game_center_authenticate;
            if (!strcmp(selector,"authenticateHandler")) return (Method)game_center_authentication_handler;
            if (!strcmp(selector,"isAuthenticated") || !strcmp(selector,"isUnderage") || !strcmp(selector,"alias") || !strcmp(selector,"displayName") || !strcmp(selector,"playerID")) return (Method)service_unavailable;
        }
    }
    if (!strcmp(receiver,"_OBJC_CLASS_$_SKPaymentQueue")) {
        if (class_receiver && !strcmp(selector,"defaultQueue")) return (Method)store_queue;
        if (class_receiver && !strcmp(selector,"canMakePayments")) return (Method)service_unavailable;
        if (!class_receiver && !strcmp(selector,"addTransactionObserver:")) return (Method)store_add_observer;
        if (!class_receiver && !strcmp(selector,"removeTransactionObserver:")) return (Method)store_remove_observer;
        if (!class_receiver && !strcmp(selector,"transactions")) return (Method)store_transactions;
        if (!class_receiver && !strcmp(selector,"addPayment:")) return (Method)store_add_payment;
        if (!class_receiver && !strcmp(selector,"finishTransaction:")) return (Method)store_finish_transaction;
    }
    return NULL;
}
