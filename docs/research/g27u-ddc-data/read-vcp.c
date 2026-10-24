#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
typedef void *(*CreateFn)(CFAllocatorRef, io_service_t);
typedef int32_t (*IOFn)(void *, uint32_t, uint32_t, uint8_t *, uint32_t);
static IOFn readfn, writefn;
static int sendpacket(void *service, uint8_t *payload, size_t n) {
    uint8_t p[64] = {0}; p[0] = 0x80 | n; memcpy(p+1,payload,n);
    uint8_t sum = 0x6e ^ 0x51;
    for(size_t i=0;i<n+1;i++) sum ^= p[i];
    p[n+1]=sum; int ok=0;
    for(int i=0;i<2;i++) { usleep(10000); int r=writefn(service,0x37,0x51,p,(uint32_t)n+2); if(r==0) ok=1; else printf("I2C write status=0x%x\n",r); }
    return ok;
}
static int receive(void *service,uint8_t *p,int n) {
    usleep(50000); int r=readfn(service,0x37,0x51,p,n);
    if(r!=0) { printf("I2C read status=0x%x\n",r); return 0; }
    int len=p[1]&0x7f; if(len<1 || len+2>=n) return 0;
    uint8_t sum=0x50; for(int i=0;i<len+2;i++) sum^=p[i];
    return sum==p[len+2];
}
int main(void) {
    setvbuf(stdout,NULL,_IOLBF,0);
    void *lib=dlopen("/System/Library/Frameworks/IOKit.framework/IOKit",RTLD_NOW);
    CreateFn create=(CreateFn)dlsym(lib,"IOAVServiceCreateWithService");
    readfn=(IOFn)dlsym(lib,"IOAVServiceReadI2C"); writefn=(IOFn)dlsym(lib,"IOAVServiceWriteI2C");
    if(!create || !readfn || !writefn) return 2;
    io_iterator_t iter=0; IOServiceGetMatchingServices(0,IOServiceMatching("DCPAVServiceProxy"),&iter);
    io_service_t entry; void *s=NULL;
    while((entry=IOIteratorNext(iter))) {
        CFTypeRef location=IORegistryEntryCreateCFProperty(entry,CFSTR("Location"),0,0);
        int external=location && CFEqual(location,CFSTR("External"));
        if(location) CFRelease(location);
        if(external) s=create(0,entry);
        IOObjectRelease(entry); if(s) break;
    }
    IOObjectRelease(iter); if(!s) return 3;
    int supported=0,unsupported=0,invalid=0;
    for(int code=0;code<256;code++) {
        uint8_t request[]={0x01,(uint8_t)code},reply[11]={0};
        int ok=sendpacket(s,request,2) && receive(s,reply,11) && reply[2]==2 && reply[4]==code;
        const char *status;
        if(!ok) { status="invalid"; invalid++; }
        else if(reply[3]!=0) { status="unsupported"; unsupported++; }
        else { status="supported"; supported++; }
        printf("VCP 0x%02X %-11s current=%5u maximum=%5u type=%u raw=",code,status,(reply[8]<<8)|reply[9],(reply[6]<<8)|reply[7],reply[5]);
        for(int i=0;i<11;i++) printf("%02X",reply[i]); puts("");
        usleep(15000);
    }
    printf("TOTAL supported=%d unsupported=%d invalid=%d\n",supported,unsupported,invalid);
    CFRelease(s); return 0;
}
