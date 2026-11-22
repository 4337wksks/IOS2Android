 
struct VulkanCompileJob { unsigned compute,done,count; VkPipelineCache cache; const void *info; VkPipeline *output; VkResult result; struct VulkanCompileJob *next; };
static struct {
    pthread_mutex_t mutex; pthread_cond_t condition; pthread_t thread;
    unsigned started; int error; struct VulkanCompileJob *first,*last;
} vulkan_compiler={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};
static void *vulkan_compile_worker(void *unused) {
    pthread_setname_np(pthread_self(),"I2A VK compile");
    pthread_attr_t attr; size_t stack_size=0;
    if (!pthread_getattr_np(pthread_self(),&attr)) { pthread_attr_getstacksize(&attr,&stack_size); pthread_attr_destroy(&attr); }
    LOG("Vulkan pipeline compiler ready: requested stack=%u bytes, reported stack=%lu bytes",16U*1024U*1024U,stack_size);
    PFN_vkCreateGraphicsPipelines graphics=(PFN_vkCreateGraphicsPipelines)vulkan.get_device_proc(vulkan.device,"vkCreateGraphicsPipelines");
    PFN_vkCreateComputePipelines compute=(PFN_vkCreateComputePipelines)vulkan.get_device_proc(vulkan.device,"vkCreateComputePipelines");
    unsigned completed=0;
    pthread_mutex_lock(&vulkan_compiler.mutex);
    for (;;) {
        while (!vulkan_compiler.first) pthread_cond_wait(&vulkan_compiler.condition,&vulkan_compiler.mutex);
        struct VulkanCompileJob *job=vulkan_compiler.first;
        vulkan_compiler.first=job->next; if (!vulkan_compiler.first) vulkan_compiler.last=NULL;
        pthread_mutex_unlock(&vulkan_compiler.mutex);
        VkResult result=VK_ERROR_INITIALIZATION_FAILED;
        if (job->compute && compute) result=compute(vulkan.device,job->cache,job->count,job->info,NULL,job->output);
        else if (!job->compute && graphics) result=graphics(vulkan.device,job->cache,job->count,job->info,NULL,job->output);
        if (++completed<=4 || !(completed&(completed-1))) LOG("Vulkan driver pipeline compiled on dedicated stack: kind=%s result=%d count=%u",job->compute ? "compute" : "graphics",result,completed);
        pthread_mutex_lock(&vulkan_compiler.mutex);
        job->result=result; job->done=1; pthread_cond_broadcast(&vulkan_compiler.condition);
    }
    return NULL;
}
static VkResult vulkan_compile_pipeline(unsigned compute,VkPipelineCache cache,uint32_t count,const void *info,VkPipeline *output) {
    struct VulkanCompileJob job={.compute=compute,.count=count,.cache=cache,.info=info,.output=output};
    pthread_mutex_lock(&vulkan_compiler.mutex);
    if (!vulkan_compiler.started) {
        vulkan_compiler.started=1;
        pthread_attr_t attr; int error=pthread_attr_init(&attr);
        if (!error) {
            error=pthread_attr_setstacksize(&attr,16U*1024U*1024U);
            if (!error) error=pthread_attr_setguardsize(&attr,64U*1024U);
            if (!error) error=pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
            if (!error) error=pthread_create(&vulkan_compiler.thread,&attr,vulkan_compile_worker,NULL);
            pthread_attr_destroy(&attr);
        }
        vulkan_compiler.error=error;
        if (error) LOG("Vulkan pipeline compiler thread creation failed: error=%d",error);
    }
    if (vulkan_compiler.error) { pthread_mutex_unlock(&vulkan_compiler.mutex); return VK_ERROR_OUT_OF_HOST_MEMORY; }
    if (vulkan_compiler.last) vulkan_compiler.last->next=&job; else vulkan_compiler.first=&job;
    vulkan_compiler.last=&job; pthread_cond_broadcast(&vulkan_compiler.condition);
    while (!job.done) pthread_cond_wait(&vulkan_compiler.condition,&vulkan_compiler.mutex);
    pthread_mutex_unlock(&vulkan_compiler.mutex); return job.result;
}
