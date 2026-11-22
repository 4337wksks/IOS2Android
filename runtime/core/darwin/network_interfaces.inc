/* Darwin ifaddrs has the same pointer-field layout on arm64, but sockaddr's
 * first two bytes and the AF_LINK address/flags require explicit conversion. */
struct DarwinInterface { struct DarwinInterface *next; char *name; unsigned flags; void *address,*netmask,*destination,*data; };
static void *darwin_interface_address(const struct sockaddr *address,const char *name) {
    if (!address) return NULL;
    if (address->sa_family==AF_INET || address->sa_family==AF_INET6) {
        size_t size=address->sa_family==AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
        unsigned char *copy=malloc(size); if (!copy) return NULL;
        memcpy(copy,address,size); copy[0]=(unsigned char)size; copy[1]=address->sa_family==AF_INET ? 2 : 30; return copy;
    }
    if (address->sa_family==AF_PACKET) {
        const struct sockaddr_ll *link=(const void *)address;
        size_t nlen=strnlen(name,IF_NAMESIZE),alen=link->sll_halen;
        if (alen>sizeof(link->sll_addr)) alen=sizeof(link->sll_addr);
        size_t size=8+nlen+alen;
        unsigned char *copy=calloc(1,size<20 ? 20 : size); if (!copy) return NULL;
        copy[0]=(unsigned char)(size<20 ? 20 : size); copy[1]=18;
        uint16_t index=(uint16_t)link->sll_ifindex; memcpy(copy+2,&index,2);
        copy[4]=link->sll_hatype==1 ? 6 : link->sll_hatype==772 ? 24 : 1;
        copy[5]=(unsigned char)nlen; copy[6]=(unsigned char)alen;
        memcpy(copy+8,name,nlen); memcpy(copy+8+nlen,link->sll_addr,alen); return copy;
    }
    return NULL;
}
static void darwin_free_interfaces(struct DarwinInterface *interfaces) {
    while (interfaces) {
        struct DarwinInterface *next=interfaces->next;
        free(interfaces->name); free(interfaces->address); free(interfaces->netmask); free(interfaces->destination); free(interfaces); interfaces=next;
    }
}
static int darwin_get_interfaces(struct DarwinInterface **output) {
    if (!output) { errno=EFAULT; return -1; }
    *output=NULL;
    struct ifaddrs *interfaces=NULL;
    if (getifaddrs(&interfaces)) return -1;
    struct DarwinInterface **slot=output;
    for (struct ifaddrs *entry=interfaces;entry;entry=entry->ifa_next) {
        struct DarwinInterface *copy=calloc(1,sizeof(*copy));
        if (!copy) { freeifaddrs(interfaces); darwin_free_interfaces(*output); *output=NULL; errno=ENOMEM; return -1; }
        copy->name=strdup(entry->ifa_name);
        copy->flags=(entry->ifa_flags&0x3ffU) | ((entry->ifa_flags&IFF_MULTICAST) ? 0x8000U : 0U);
        copy->address=darwin_interface_address(entry->ifa_addr,entry->ifa_name);
        copy->netmask=darwin_interface_address(entry->ifa_netmask,entry->ifa_name);
        copy->destination=darwin_interface_address(entry->ifa_broadaddr,entry->ifa_name);
        *slot=copy; slot=&copy->next;
    }
    freeifaddrs(interfaces); return 0;
}
static const char *darwin_inet_ntop(int family,const void *address,char *buffer,socklen_t size) { return inet_ntop(family==30 ? AF_INET6 : family,address,buffer,size); }
static int darwin_inet_pton(int family,const char *text,void *address) { return inet_pton(family==30 ? AF_INET6 : family,text,address); }
static Word resolve_interfaces_import(const char *name) {
    if (!strcmp(name,"_getifaddrs")) return (Word)darwin_get_interfaces;
    if (!strcmp(name,"_freeifaddrs")) return (Word)darwin_free_interfaces;
    if (!strcmp(name,"_inet_ntop")) return (Word)darwin_inet_ntop;
    if (!strcmp(name,"_inet_pton")) return (Word)darwin_inet_pton;
    if (!strcmp(name,"_inet_ntoa")) return (Word)inet_ntoa;
    if (!strcmp(name,"_if_nametoindex")) return (Word)if_nametoindex;
    if (!strcmp(name,"_if_indextoname")) return (Word)if_indextoname;
    return 0;
}
