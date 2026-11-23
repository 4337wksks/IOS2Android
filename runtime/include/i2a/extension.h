#ifndef I2A_EXTENSION_H
#define I2A_EXTENSION_H
#include <stddef.h>
#include <stdint.h>

#define I2A_RUNTIME_INTERFACE 2
typedef uintptr_t I2AObject;
typedef I2AObject (*I2AMethod)(I2AObject,const char *,I2AObject,I2AObject,I2AObject,I2AObject);
struct I2ADisplay { int width,height; double scale; };
struct I2AModalAction { I2AObject title; unsigned style; I2AObject attributed_title; };
struct I2AModal {
    I2AObject title,message;
    unsigned style,count;
    struct I2AModalAction actions[8];
    /* Optional adapter-owned presentation. Text attributes use the UIKit bridge. */
    const char *presentation,*image_name;
    I2AObject attributed_title,attributed_message;
};
struct I2ARuntimeAPI {
    unsigned version,size;
    uintptr_t image_base;
    I2AObject (*root_controller)(void);
    I2AObject (*first_responder)(void);
    struct I2ADisplay (*display)(void);
    const char *(*class_name)(I2AObject);
    I2AObject (*class_named)(const char *);
    I2AMethod (*find_method)(I2AObject,const char *);
    I2AObject (*attributed_text)(I2AObject);
    I2AObject (*copy_text)(const char *);
    I2AObject (*empty_products_response)(I2AObject);
    void (*fail)(const char *,const char *);
};
struct I2AExtension {
    unsigned version,size;
    I2AMethod (*wrap_method)(I2AMethod);
    int (*filter_touch)(int,int,double,float,float);
    void (*cancel_touches)(void);
    int (*describe_modal)(I2AObject,struct I2AModal *);
    void (*complete_modal)(I2AObject,unsigned);
    void (*close_unavailable_controller)(I2AObject,const char *);
    int (*use_platform_trust)(const char *);
    I2AObject (*unavailable_products_response)(I2AObject);
    void (*label_diagnostic)(I2AObject);
    void (*scene_diagnostic)(void);
    void (*shader_buffer_diagnostic)(const char *,const void *,size_t,const float *);
};

/* Registered once before guest entry. Guest callbacks stay on their caller's owner. */
const struct I2AExtension *i2a_extension_initialize(const struct I2ARuntimeAPI *);
#endif
