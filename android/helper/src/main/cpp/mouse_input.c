#include <jni.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#define WORD_BITS (sizeof(unsigned long)*8)
#define WORDS(max) (((max)+WORD_BITS)/WORD_BITS)
struct mouse { int fd,output,buttons,out_buttons,dx,dy,h,v,report_events; bool syncing; };
static const int codes[]={BTN_LEFT,BTN_RIGHT,BTN_MIDDLE,BTN_SIDE,BTN_EXTRA};
static bool bit(const unsigned long *bits,int index){return (bits[index/WORD_BITS]>>(index%WORD_BITS))&1UL;}
static void error(JNIEnv *env,const char *message){(*env)->ThrowNew(env,(*env)->FindClass(env,"java/io/IOException"),message);}
static bool inspect(int fd,char *identity,size_t size){
    struct input_id id;struct stat st;char name[128]={0};
    unsigned long rel[WORDS(REL_MAX)]={0},key[WORDS(KEY_MAX)]={0};
    if(ioctl(fd,EVIOCGID,&id)<0||ioctl(fd,EVIOCGNAME(sizeof(name)),name)<0||fstat(fd,&st)<0
            ||ioctl(fd,EVIOCGBIT(EV_REL,sizeof(rel)),rel)<0||ioctl(fd,EVIOCGBIT(EV_KEY,sizeof(key)),key)<0
            ||!bit(rel,REL_X)||!bit(rel,REL_Y)||!bit(key,BTN_LEFT)||bit(key,KEY_A)||bit(key,KEY_ENTER)
            ||strncmp(name,"ms912x-",7)==0)return false;
    name[sizeof(name)-1]='\0';
    for(char *p=name;*p;p++)if((unsigned char)*p<32||(unsigned char)*p>=127)*p=' ';
    snprintf(identity,size,"%u:%u:%u:%u:%llu:%s",id.bustype,id.vendor,id.product,id.version,(unsigned long long)st.st_rdev,name);
    return true;
}
JNIEXPORT jobjectArray JNICALL Java_io_github_ms912x_helper_NativeMouse_listNative(JNIEnv *env,jclass type){
    (void)type;char *values[64]={0};int count=0;DIR *directory=opendir("/dev/input");
    if(directory){struct dirent *entry;while(count<64&&(entry=readdir(directory))){
        if(strncmp(entry->d_name,"event",5)!=0)continue;
        char path[512],identity[320],value[850];snprintf(path,sizeof(path),"/dev/input/%s",entry->d_name);
        int fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);if(fd<0)continue;
        if(inspect(fd,identity,sizeof(identity))){snprintf(value,sizeof(value),"%s\t%s",path,identity);values[count]=strdup(value);if(values[count])count++;}
        close(fd);
    }closedir(directory);}
    jobjectArray result=(*env)->NewObjectArray(env,count,(*env)->FindClass(env,"java/lang/String"),NULL);
    for(int i=0;i<count;i++){if(result&&!(*env)->ExceptionCheck(env)){jstring value=(*env)->NewStringUTF(env,values[i]);if(value){(*env)->SetObjectArrayElement(env,result,i,value);(*env)->DeleteLocalRef(env,value);}}free(values[i]);}
    return result;
}
static int create_output(void){
    int fd=open("/dev/uinput",O_WRONLY|O_NONBLOCK|O_CLOEXEC);if(fd<0)return -1;
    if(ioctl(fd,UI_SET_EVBIT,EV_KEY)<0||ioctl(fd,UI_SET_EVBIT,EV_REL)<0)goto fail;
    for(unsigned int i=0;i<sizeof(codes)/sizeof(*codes);i++)if(ioctl(fd,UI_SET_KEYBIT,codes[i])<0)goto fail;
    for(int axis=REL_X;axis<=REL_Y;axis++)if(ioctl(fd,UI_SET_RELBIT,axis)<0)goto fail;
    if(ioctl(fd,UI_SET_RELBIT,REL_WHEEL)<0||ioctl(fd,UI_SET_RELBIT,REL_HWHEEL)<0)goto fail;
    struct uinput_setup config={0};strcpy(config.name,"ms912x-shared-mouse");config.id.bustype=BUS_VIRTUAL;config.id.vendor=0x534d;config.id.product=0x9120;
    if(ioctl(fd,UI_DEV_SETUP,&config)<0||ioctl(fd,UI_DEV_CREATE)<0)goto fail;
    return fd;
fail:close(fd);return -1;
}
JNIEXPORT jlong JNICALL Java_io_github_ms912x_helper_NativeMouse_openNative(JNIEnv *env,jclass type,jstring selected){
    (void)type;if(getuid()!=2000){error(env,"Mouse capture requires shell UID 2000");return 0;}
    const char *value=(*env)->GetStringUTFChars(env,selected,NULL);if(!value)return 0;
    char *copy=strdup(value);(*env)->ReleaseStringUTFChars(env,selected,value);if(!copy){error(env,"Out of memory");return 0;}
    char *identity=strchr(copy,'\t');int fd=-1,output=-1;char current[320];unsigned long held[WORDS(KEY_MAX)]={0};
    if(!identity){error(env,"Invalid mouse identity");goto fail;}*identity++=0;
    if(strncmp(copy,"/dev/input/event",16)!=0||!copy[16]||strspn(copy+16,"0123456789")!=strlen(copy+16)){error(env,"Invalid mouse path");goto fail;}
    fd=open(copy,O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0||!inspect(fd,current,sizeof(current))||strcmp(identity,current)){error(env,"Selected mouse changed or disappeared");goto fail;}
    if(ioctl(fd,EVIOCGKEY(sizeof(held)),held)<0){error(env,"Cannot check mouse buttons");goto fail;}
    for(int code=BTN_LEFT;code<=BTN_TASK;code++)if(bit(held,code)){error(env,"Release mouse buttons before sharing");goto fail;}
    output=create_output();if(output<0){error(env,"Android refused virtual mouse creation");goto fail;}
    if(ioctl(fd,EVIOCGRAB,1)<0){error(env,"Android refused exclusive mouse capture");goto fail;}
    struct mouse *mouse=calloc(1,sizeof(*mouse));if(!mouse){error(env,"Out of memory");goto fail;}
    mouse->fd=fd;mouse->output=output;free(copy);return (jlong)(intptr_t)mouse;
fail:if(fd>=0)close(fd);if(output>=0){ioctl(output,UI_DEV_DESTROY);close(output);}free(copy);return 0;
}
JNIEXPORT void JNICALL Java_io_github_ms912x_helper_NativeMouse_beginNative(JNIEnv *env,jclass type,jlong handle){
    (void)type;struct mouse *mouse=(void *)(intptr_t)handle;struct input_event events[128];
    for(int batch=0;batch<64;batch++){
        ssize_t count=read(mouse->fd,events,sizeof(events));
        if(count<0&&errno==EINTR)continue;
        if(count<0&&errno==EAGAIN){
            unsigned long held[WORDS(KEY_MAX)]={0};
            if(ioctl(mouse->fd,EVIOCGKEY(sizeof(held)),held)<0){error(env,"Mouse disappeared during setup");return;}
            for(int code=BTN_LEFT;code<=BTN_TASK;code++)if(bit(held,code)){error(env,"Release mouse buttons before sharing");return;}
            mouse->buttons=mouse->dx=mouse->dy=mouse->h=mouse->v=mouse->report_events=0;return;
        }
        if(count<=0){error(env,"Mouse disappeared during setup");return;}
    }
    error(env,"Mouse did not settle during setup");
}
JNIEXPORT jintArray JNICALL Java_io_github_ms912x_helper_NativeMouse_readNative(JNIEnv *env,jclass type,jlong handle){
    (void)type;struct mouse *mouse=(void *)(intptr_t)handle;
    struct pollfd poller={.fd=mouse->fd,.events=POLLIN};int ready=poll(&poller,1,16);
    if(ready<0&&errno==EINTR)return NULL;
    if(ready<0||(ready>0&&(poller.revents&(POLLERR|POLLHUP|POLLNVAL)))){error(env,"Mouse disconnected");return NULL;}
    if(!ready)return NULL;
    /* Leave later reports in the kernel so buttons and their SYN boundaries retain ordering. */
    for(int count=0;count<1024;count++){
        struct input_event event;ssize_t bytes=read(mouse->fd,&event,sizeof(event));
        if(bytes<0&&errno==EAGAIN)return NULL;
        if(bytes<0&&errno==EINTR)continue;
        if(bytes!=sizeof(event)){error(env,"Mouse read failed");return NULL;}
        if(event.type==EV_SYN&&event.code==SYN_DROPPED){
            mouse->syncing=true;mouse->dx=mouse->dy=mouse->h=mouse->v=mouse->report_events=0;
            continue;
        }
        if(mouse->syncing){
            /* evdev requires discarding through SYN_REPORT, then querying current key state. */
            if(event.type!=EV_SYN||event.code!=SYN_REPORT)continue;
            unsigned long held[WORDS(KEY_MAX)]={0};
            if(ioctl(mouse->fd,EVIOCGKEY(sizeof(held)),held)<0){error(env,"Mouse state recovery failed");return NULL;}
            mouse->buttons=0;
            for(unsigned int i=0;i<sizeof(codes)/sizeof(*codes);i++)if(bit(held,codes[i]))mouse->buttons|=1<<i;
            mouse->syncing=false;
            jint values[]={0,0,mouse->buttons,0,0,1};
            jintArray result=(*env)->NewIntArray(env,6);
            if(result)(*env)->SetIntArrayRegion(env,result,0,6,values);return result;
        }
        if(++mouse->report_events>1024){error(env,"Mouse report exceeded event limit");return NULL;}
        if(event.type==EV_REL){
            /* Bound malicious devices without integer overflow. High-resolution wheel duplicates are ignored. */
            int delta=event.value<-32768?-32768:event.value>32768?32768:event.value;
            if(event.code==REL_X)mouse->dx+=delta;if(event.code==REL_Y)mouse->dy+=delta;
            if(event.code==REL_HWHEEL)mouse->h+=delta;if(event.code==REL_WHEEL)mouse->v+=delta;
        }
        if(event.type==EV_KEY)for(unsigned int i=0;i<sizeof(codes)/sizeof(*codes);i++)if(event.code==codes[i]){
            if(event.value)mouse->buttons|=1<<i;else mouse->buttons&=~(1<<i);
        }
        if(event.type==EV_SYN&&event.code==SYN_REPORT){
            jint values[]={mouse->dx,mouse->dy,mouse->buttons,mouse->h,mouse->v};
            mouse->dx=mouse->dy=mouse->h=mouse->v=mouse->report_events=0;jintArray result=(*env)->NewIntArray(env,5);
            if(result)(*env)->SetIntArrayRegion(env,result,0,5,values);return result;
        }
    }
    if(!mouse->syncing)error(env,"Mouse report exceeded event limit");return NULL;
}
JNIEXPORT void JNICALL Java_io_github_ms912x_helper_NativeMouse_emitNative(JNIEnv *env,jclass type,jlong handle,jint dx,jint dy,jint buttons,jint horizontal,jint vertical){
    (void)type;struct mouse *mouse=(void *)(intptr_t)handle;struct input_event events[10]={0};int count=0;
    for(int i=0;i<5;i++)if((buttons^mouse->out_buttons)&(1<<i))events[count++]=(struct input_event){.type=EV_KEY,.code=codes[i],.value=!!(buttons&(1<<i))};
    if(dx)events[count++]=(struct input_event){.type=EV_REL,.code=REL_X,.value=dx};
    if(dy)events[count++]=(struct input_event){.type=EV_REL,.code=REL_Y,.value=dy};
    if(horizontal)events[count++]=(struct input_event){.type=EV_REL,.code=REL_HWHEEL,.value=horizontal};
    if(vertical)events[count++]=(struct input_event){.type=EV_REL,.code=REL_WHEEL,.value=vertical};
    events[count++]=(struct input_event){.type=EV_SYN,.code=SYN_REPORT};
    ssize_t size=count*sizeof(*events);if(write(mouse->output,events,size)!=size){error(env,"Virtual mouse write failed");return;}mouse->out_buttons=buttons;
}
JNIEXPORT void JNICALL Java_io_github_ms912x_helper_NativeMouse_closeNative(JNIEnv *env,jclass type,jlong handle){
    (void)env;(void)type;struct mouse *mouse=(void *)(intptr_t)handle;
    struct input_event releases[6]={0};int count=0;
    for(int i=0;i<5;i++)if(mouse->out_buttons&(1<<i))releases[count++]=(struct input_event){.type=EV_KEY,.code=codes[i],.value=0};
    releases[count++]=(struct input_event){.type=EV_SYN,.code=SYN_REPORT};
    (void)write(mouse->output,releases,count*sizeof(*releases));
    ioctl(mouse->fd,EVIOCGRAB,0);close(mouse->fd);ioctl(mouse->output,UI_DEV_DESTROY);close(mouse->output);free(mouse);
}
