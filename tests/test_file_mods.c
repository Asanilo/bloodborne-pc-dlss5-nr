/* Exercise actual guest file operations on a linked overlay and writable saves. */
#include "../src/runtime_file.c"
#include <assert.h>
static int32_t guest_errno;
int32_t *runtime_errno(void) { return &guest_errno; }
int32_t runtime_guest_errno(int e) { return e; }
/* Reads into guest memory tell the GPU side (runtime_memory.c); nothing to tell here. */
void runtime_memory_note_write(uintptr_t address, uint64_t size) { (void)address; (void)size; }
uintptr_t runtime_lookup(const RuntimeExport *table,size_t n,const char *name) {
    for (size_t i=0;i<n;++i) if (!strcmp(table[i].name,name)) return (uintptr_t)table[i].function;
    return 0;
}
int main(void) {
    char root[]="/tmp/bbport-mod-files-XXXXXX";
    assert(mkdtemp(root));
    char game[512],user[512],source[512],link[512];
    snprintf(game,sizeof(game),"%s/game",root);
    snprintf(user,sizeof(user),"%s/user",root);
    snprintf(source,sizeof(source),"%s/mod.dcx",root);
    snprintf(link,sizeof(link),"%s/game/asset.dcx",root);
    assert(!mkdir(game,0755));
    FILE *f=fopen(source,"w"); assert(f); assert(fputs("modded",f)>=0); assert(!fclose(f));
    assert(!symlink(source,link));
    runtime_file_configure(game,user);
    // The five base mounts must leave room for all sixteen SaveData mount points.
    for (int i=0;i<16;++i) {
        char guest[64],translated[1024],expected[1024];
        snprintf(guest,sizeof(guest),"/savedata%d",i);
        assert(!runtime_file_mount(guest,user));
        snprintf(guest,sizeof(guest),"/savedata%d/slot-%d",i,i);
        assert(!translate(guest,translated,sizeof(translated)));
        snprintf(expected,sizeof(expected),"%s/slot-%d",user,i);
        assert(!strcmp(translated,expected));
    }
    // Exhaustion refuses a new name, keeps old mappings, and unmount frees capacity.
    for (int i=0;i<MAX_MOUNTS-21;++i) {
        char guest[64]; snprintf(guest,sizeof(guest),"/capacity%d",i);
        assert(!runtime_file_mount(guest,user));
    }
    assert(runtime_file_mount("/overflow",user)==-1);
    assert(!runtime_file_mount("/savedata15",user)); // replacement at capacity
    runtime_file_unmount("/savedata15");
    assert(!runtime_file_mount("/overflow",user));
    for (int i=0;i<16;++i) {
        char guest[64]; snprintf(guest,sizeof(guest),"/savedata%d",i);
        runtime_file_unmount(guest);
    }
    for (int i=0;i<MAX_MOUNTS-21;++i) {
        char guest[64]; snprintf(guest,sizeof(guest),"/capacity%d",i);
        runtime_file_unmount(guest);
    }
    runtime_file_unmount("/overflow");
    for (int i=0;i<3;++i) {
        const char *path=i==0 ? "/app0/asset.dcx" : i==1 ? "/hostapp/asset.dcx" : "asset.dcx";
        int fd=(int)do_open(path,0,0); assert(fd>=3);
        char content[8]={0}; assert(do_read(fd,content,6)==6 && !strcmp(content,"modded"));
        GuestStat info; assert(!do_stat(path,&info) && info.size==6);
        assert(!do_close(fd));
        assert(do_open(path,2,0)==-EROFS);
        assert(do_open(path,0x400,0)==-EROFS);
        assert(do_truncate(path,0)==-EROFS);
        assert(path_op(path,2,0)==-EROFS);
        assert(do_rename(path,"/data/moved")==-EROFS);
    }
    int dir=(int)do_open("/app0",0x20000,0); assert(dir>=3);
    char entries[1024]; int64_t count=do_getdents(dir,entries,sizeof(entries),NULL); assert(count>0);
    int found=0;
    for (int64_t p=0;p<count;) {
        uint16_t length; memcpy(&length,entries+p+4,2);
        assert(length);
        if (!strcmp(entries+p+8,"asset.dcx")) { assert(entries[p+6]==8); found=1; }
        p+=length;
    }
    assert(found && !do_close(dir));
    int save=(int)do_open("/data/test-save",0x202,0644); assert(save>=3);
    assert(do_write(save,"save",4)==4 && !do_close(save));
    assert(!path_op("/data/test-save",2,0));
    assert(!unlink(link) && !unlink(source) && !rmdir(game));
    const char *dirs[]={"temp0","download0","data"};
    for (int i=0;i<3;++i) { char p[1024]; snprintf(p,sizeof(p),"%s/%s",user,dirs[i]); assert(!rmdir(p)); }
    assert(!rmdir(user) && !rmdir(root));
    puts("Guest mod files: 16 save mounts, capacity/reuse, reads, stat, merged listing, readonly assets and writable saves PASS");
}
