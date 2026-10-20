/* Actual runtime_content add-on mount/list/info/unmount against the actual runtime_file table.
 * Temporary directories only: no game/DLC assets or saves. */
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif
#include "../src/runtime_file.c"
#include "runtime.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int32_t guest_errno;
int32_t *runtime_errno(void) { return &guest_errno; }
int32_t runtime_guest_errno(int e) { return e; }
void runtime_memory_note_write(uintptr_t address, uint64_t size) { (void)address; (void)size; }
uintptr_t runtime_lookup(const RuntimeExport *table,size_t n,const char *name) {
    for (size_t i=0;i<n;++i) if (!strcmp(table[i].name,name)) return (uintptr_t)table[i].function;
    return 0;
}

typedef int32_t (ABI *Module)(uint16_t);
typedef int32_t (ABI *Init)(const void *,void *);
typedef int32_t (ABI *ListFn)(uint32_t,void *,uint32_t,uint32_t *);
typedef int32_t (ABI *MountFn)(uint32_t,const char *,char *);
typedef int32_t (ABI *UnmountFn)(const char *);
typedef int32_t (ABI *InfoFn)(uint32_t,const char *,void *);
#define GET(t,n) ((t)runtime_content_resolve(n))
#define PARAMETER ((int32_t)0x80d90002)
#define BUSY ((int32_t)0x80d90003)
#define NOT_FOUND ((int32_t)0x80d90005)

static char root[512], user[512], game[512], title[32];
static ListFn list_addons;
static MountFn mount_addon;
static UnmountFn unmount_addon;
static InfoFn info_addon;
static Module module_load, module_unload;
static Init content_init;

static void make_dir(const char *path) {
    char copy[512];
    snprintf(copy,sizeof(copy),"%s",path);
    for (char *p=copy+1;*p;++p) if (*p=='/') { *p=0; if (mkdir(copy,0755) && errno!=EEXIST) assert(0); *p='/'; }
    if (mkdir(copy,0755) && errno!=EEXIST) assert(0);
}
static void make_label(const char *title_name,const char *label) {
    char path[512];
    snprintf(path,sizeof(path),"%s/addcont/%s",user,title_name); make_dir(path);
    snprintf(path,sizeof(path),"%s/addcont/%s/%s",user,title_name,label); make_dir(path);
}
static void set_title(const char *value) {
    snprintf(title,sizeof(title),"%s",value);
    runtime_content_set_title(title);
}
static void check_point(const char *point,const char *want) {
    assert(!strcmp(point,want));
}
static void check_canary(const unsigned char *p,size_t from,size_t to,unsigned char value) {
    for (size_t i=from;i<to;++i) assert(p[i]==value);
}
static void *mount_thread(void *argument) {
    const char *label=argument;
    for (int i=0;i<200;++i) {
        char point[32];
        assert(mount_addon(0,label,point)==0);
        assert(!strncmp(point,"/addcont",8));
        assert(unmount_addon(point)==0);
    }
    return NULL;
}
int main(void) {
    snprintf(root,sizeof(root),"/tmp/bbport-dlc-XXXXXX"); assert(mkdtemp(root));
    snprintf(user,sizeof(user),"%s/user",root); snprintf(game,sizeof(game),"%s/game",root);
    make_dir(user); make_dir(game);
    runtime_file_configure(game,user);
    runtime_content_configure((const uint32_t[]){1,13,0,0,7});
    module_load=GET(Module,"g8cM39EUZ6o#M#N"); module_unload=GET(Module,"eR2bZFAAU0Q#M#N");
    content_init=GET(Init,"R9lA82OraNs#c#d"); list_addons=GET(ListFn,"xnd8BJzAxmk#c#d");
    mount_addon=GET(MountFn,"VANhIWcqYak#c#d"); unmount_addon=GET(UnmountFn,"3rHWaV-1KC4#c#d");
    info_addon=GET(InfoFn,"m47juOmH0VE#c#d");
    assert(module_load && module_unload && content_init && list_addons && mount_addon && unmount_addon && info_addon);
    assert(module_load(0xb4)==0); unsigned char boot[40]; assert(content_init((const unsigned char[32]){0},boot)==0);

    /* Wrong title is not scanned; a label under another title is invisible. */
    make_label("TITLE_OTHER","OTHER_DLC");
    set_title("TITLE_MAIN");
    uint32_t hits=0;
    assert(list_addons(0,NULL,0,&hits)==0 && hits==0);
    char point[32]; memset(point,0xaa,sizeof(point));
    assert(mount_addon(0,"OTHER_DLC",point)==NOT_FOUND);
    check_canary((unsigned char *)point,0,sizeof(point),0xaa);
    runtime_content_set_title("../TITLE_MAIN");
    assert(list_addons(0,NULL,0,&hits)==0 && hits==0);
    runtime_content_set_title("TITLE_MAIN");
    char title_link[512];
    snprintf(title_link,sizeof(title_link),"%s/addcont/TITLE_LINK",user);
    assert(!symlink("TITLE_MAIN",title_link));
    runtime_content_set_title("TITLE_LINK");
    assert(list_addons(0,NULL,0,&hits)==0 && hits==0);
    assert(mount_addon(0,"GOOD_DLC",point)==NOT_FOUND);
    runtime_content_set_title("TITLE_MAIN");
    assert(!unlink(title_link));

    /* Invalid service/label and a symlink escape never publish a point. */
    make_label("TITLE_MAIN","GOOD_DLC");
    memset(point,0xaa,sizeof(point));
    assert(mount_addon(1,"GOOD_DLC",point)==PARAMETER);
    assert(mount_addon(0,"../escape",point)==PARAMETER);
    assert(mount_addon(0,"BAD/LABEL",point)==PARAMETER);
    assert(mount_addon(0,"TOO_LONG_LABEL_123",point)==PARAMETER);
    char link_path[512], outside[]="/tmp";
    snprintf(link_path,sizeof(link_path),"%s/addcont/TITLE_MAIN/ESCAPE",user);
    assert(!symlink(outside,link_path));
    assert(mount_addon(0,"ESCAPE",point)==NOT_FOUND);
    check_canary((unsigned char *)point,0,sizeof(point),0xaa);

    /* Host mount failure leaves the slot and output untouched; retry succeeds after capacity. */
    make_label("TITLE_FAIL","FAIL_DLC");
    set_title("TITLE_FAIL");
    for (int i=0;i<MAX_MOUNTS-5;++i) {
        char guest[64]; snprintf(guest,sizeof(guest),"/capacity%d",i);
        assert(!runtime_file_mount(guest,user));
    }
    memset(point,0xaa,sizeof(point));
    assert(mount_addon(0,"FAIL_DLC",point)==BUSY);
    check_canary((unsigned char *)point,0,sizeof(point),0xaa);
    runtime_file_unmount("/capacity0");
    assert(mount_addon(0,"FAIL_DLC",point)==0);
    check_point(point,"/addcont0");
    assert(unmount_addon("/addcont0")==0);
    for (int i=1;i<MAX_MOUNTS-5;++i) { char guest[64]; snprintf(guest,sizeof(guest),"/capacity%d",i); runtime_file_unmount(guest); }

    /* Current title lists only its labels, deterministically, with output canaries. */
    make_label("TITLE_B","B_DLC"); make_label("TITLE_B","A_DLC");
    set_title("TITLE_B");
    unsigned char entries[48]; memset(entries,0xaa,sizeof(entries));
    hits=0;
    assert(list_addons(0,entries,2,&hits)==0 && hits==2);
    assert(!strcmp((const char *)entries,"A_DLC"));
    assert(!strcmp((const char *)entries+24,"B_DLC"));
    memset(entries,0xaa,sizeof(entries));
    assert(list_addons(0,entries,1,&hits)==0 && hits==2);
    assert(!strcmp((const char *)entries,"A_DLC"));
    check_canary(entries,24,sizeof(entries),0xaa);

    /* Sixteen slots, duplicate refs, third unmount, and slot reuse. */
    char labels[16][16];
    for (int i=0;i<16;++i) { snprintf(labels[i],sizeof(labels[i]),"DLCLABEL%02d",i); make_label("TITLE_SLOTS",labels[i]); }
    make_label("TITLE_SLOTS","DLCLABEL17");
    set_title("TITLE_SLOTS");
    char points[16][32];
    for (int i=0;i<16;++i) assert(mount_addon(0,labels[i],points[i])==0);
    memset(point,0xaa,sizeof(point));
    assert(mount_addon(0,"DLCLABEL17",point)==BUSY); /* no free slot; no output */
    check_canary((unsigned char *)point,0,sizeof(point),0xaa);
    char duplicate[32]; assert(mount_addon(0,labels[0],duplicate)==0 && !strcmp(duplicate,points[0]));
    assert(unmount_addon(points[0])==0); assert(mount_addon(0,labels[0],duplicate)==0); assert(unmount_addon(points[0])==0);
    assert(unmount_addon(points[0])==0);
    assert(unmount_addon(points[0])==NOT_FOUND);
    for (int i=1;i<16;++i) assert(unmount_addon(points[i])==0);
    make_label("TITLE_SLOTS","REUSE_DLC");
    assert(mount_addon(0,"REUSE_DLC",point)==0 && !strcmp(point,"/addcont0"));
    assert(unmount_addon(point)==0);

    /* A file opened before unmount remains valid (POSIX descriptor), but the path is gone. */
    set_title("TITLE_MAIN");
    char file_path[512];
    snprintf(file_path,sizeof(file_path),"%s/addcont/TITLE_MAIN/GOOD_DLC/file.bin",user);
    FILE *f=fopen(file_path,"wb"); assert(f); assert(fputs("dlc-data",f)>=0); assert(!fclose(f));
    assert(mount_addon(0,"GOOD_DLC",point)==0); check_point(point,"/addcont0");
    assert(point[15]==0);
    check_canary((unsigned char *)point,16,sizeof(point),0xaa);
    struct { unsigned char bytes[24]; uint32_t canary; } addon_info_buffer;
    memset(&addon_info_buffer,0xaa,sizeof(addon_info_buffer));
    assert(info_addon(0,"GOOD_DLC",addon_info_buffer.bytes)==0);
    assert(!strcmp((const char *)addon_info_buffer.bytes,"GOOD_DLC"));
    for (unsigned i=16;i<20;++i) assert(addon_info_buffer.bytes[i]==0);
    assert(addon_info_buffer.canary==0xaaaaaaaau);
    memset(&addon_info_buffer,0xaa,sizeof(addon_info_buffer));
    assert(info_addon(1,"GOOD_DLC",addon_info_buffer.bytes)==PARAMETER);
    assert(info_addon(0,"BAD/LABEL",addon_info_buffer.bytes)==PARAMETER);
    assert(info_addon(0,"MISSING",addon_info_buffer.bytes)==NOT_FOUND);
    check_canary(addon_info_buffer.bytes,0,sizeof(addon_info_buffer.bytes),0xaa);
    char badpoint[32];
    memset(badpoint,0xaa,sizeof(badpoint));
    assert(unmount_addon("/addcont16")==PARAMETER);
    assert(unmount_addon("/addcont00")==PARAMETER);
    assert(unmount_addon("/addcont0/child")==PARAMETER);
    check_canary((unsigned char *)badpoint,0,sizeof(badpoint),0xaa);
    int fd=(int)do_open("/addcont0/file.bin",0,0); assert(fd>=3);
    char readback[16]={0}; assert(do_read(fd,readback,8)==8 && !strcmp(readback,"dlc-data"));
    assert(do_open("/addcont0/file.bin",1,0)==-EROFS);
    assert(do_open("/addcont0/new.bin",0x202,0644)==-EROFS);
    assert(do_open("/addcont0/file.bin",0x400,0)==-EROFS);
    assert(path_op("/addcont0/file.bin",2,0)==-EROFS);
    assert(path_op("/addcont0/newdir",0,0755)==-EROFS);
    assert(path_op("/addcont0",1,0)==-EROFS);
    assert(do_rename("/addcont0/file.bin","/data/moved")==-EROFS);
    assert(do_rename("/data/moved","/addcont0/file.bin")==-EROFS);
    assert(do_truncate("/addcont0/file.bin",0)==-EROFS);
    assert(do_access("/addcont0/file.bin",W_OK)==-EROFS);
    assert(unmount_addon(point)==0);
    assert(do_lseek(fd,0,SEEK_SET)==0);
    memset(readback,0,sizeof(readback));
    assert(do_read(fd,readback,8)==8 && !strcmp(readback,"dlc-data"));
    assert(do_close(fd)==0);
    assert(do_open("/addcont0/file.bin",0,0)==-ENOENT);

    /* Save/data writes are not blocked by the add-on guard. */
    int save=(int)do_open("/data/save-test",0x202,0644); assert(save>=3);
    assert(do_write(save,"save",4)==4 && do_close(save)==0);
    assert(!path_op("/data/save-test",2,0));

    /* Concurrent duplicate mount/unmount is serialized and balanced. */
    set_title("TITLE_THREAD");
    make_label("TITLE_THREAD","THREAD_DLC");
    pthread_t threads[4];
    for (int i=0;i<4;++i) assert(!pthread_create(&threads[i],NULL,mount_thread,"THREAD_DLC"));
    for (int i=0;i<4;++i) assert(!pthread_join(threads[i],NULL));
    memset(point,0xaa,sizeof(point));
    assert(mount_addon(0,"THREAD_DLC",point)==0); assert(unmount_addon(point)==0);

    /* A symlinked addcont root escaping <user> is rejected before listing/mounting. */
    {
        char addcont[512], real[512], outside[512];
        snprintf(addcont,sizeof(addcont),"%s/addcont",user);
        snprintf(real,sizeof(real),"%s/addcont.real",user);
        snprintf(outside,sizeof(outside),"%s/outside",root);
        make_dir(outside);
        assert(!rename(addcont,real));
        assert(!symlink(outside,addcont));
        set_title("TITLE_MAIN");
        hits=99;
        assert(list_addons(0,NULL,0,&hits)==0 && hits==0);
        assert(mount_addon(0,"GOOD_DLC",point)==NOT_FOUND);
        assert(!unlink(addcont));
        assert(!rename(real,addcont));
        rmdir(outside);
    }
    /* Final AppContent unload retires active mounts; the old guest FD stays readable, and a
       later reload starts with a clean slot0. */
    set_title("TITLE_MAIN");
    assert(mount_addon(0,"GOOD_DLC",point)==0);
    int lifecycle_fd=(int)do_open("/addcont0/file.bin",0,0); assert(lifecycle_fd>=3);
    assert(module_unload(0xb4)==0);
    assert(do_open("/addcont0/file.bin",0,0)==-ENOENT);
    assert(do_lseek(lifecycle_fd,0,SEEK_SET)==0);
    memset(readback,0,sizeof(readback));
    assert(do_read(lifecycle_fd,readback,8)==8 && !strcmp(readback,"dlc-data"));
    assert(do_close(lifecycle_fd)==0);
    assert(module_load(0xb4)==0);
    assert(content_init((const unsigned char[32]){0},boot)==0);
    assert(mount_addon(0,"GOOD_DLC",point)==0 && !strcmp(point,"/addcont0"));
    assert(unmount_addon(point)==0);

    assert(module_unload(0xb4)==0);
    puts("PASS: actual DLC title/label/service, mount failure/reuse/duplicate, read-only and POSIX-FD edges");
    return 0;
}
