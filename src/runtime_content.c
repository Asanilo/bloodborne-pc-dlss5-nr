/* Offline AppContent provider for the configured base-game profile.
 * No package/DLC downloads, entitlement or license emulation; add-on folders in
 * <user>/addcont/<current title> are reported installed and their files mounted read-only. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "runtime.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define INVALID_ID ((int32_t)0x805a1000)
#define NOT_LOADED ((int32_t)0x805a1001)
#define PARAMETER ((int32_t)0x80d90002)
#define BUSY ((int32_t)0x80d90003)
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static unsigned refs, initialized, configured;
static uint32_t parameters[5];
static unsigned queries, lists;
static char title[16]; /* CUSA...: add-on folders are <user>/addcont/<this title>/<label> */
#define LABEL_SIZE 17
#define ADDON_SLOTS 16
typedef struct {
    unsigned used, refs;
    char label[LABEL_SIZE];
    char guest[16];
} AddonMount;
static AddonMount addons[ADDON_SLOTS];
static unsigned addons_mounted;
void runtime_content_configure(const uint32_t values[5]) {
    pthread_mutex_lock(&lock);
    if (refs || initialized || (values[0]!=1 && values[0]!=3)) {
        fputs("ERROR: invalid or late content profile\n",stderr); exit(1);
    }
    memcpy(parameters,values,sizeof(parameters)); configured=1;
    pthread_mutex_unlock(&lock);
}
static void require_provider(void) {
    if (!configured) { fputs("STOP: AppContent needs --content-profile\n",stderr); exit(21); }
}
/* System modules: every library this eboot imports is provided by the host
 * runtime, so loading one only tracks a reference count. AppContent (0xb4)
 * additionally requires the configured content profile. */
static unsigned module_refs[0x10000];
static ABI int32_t module_load(uint16_t id) {
    if (!id) return INVALID_ID;
    if (id==0xb4) require_provider();
    pthread_mutex_lock(&lock);
    if (module_refs[id]==UINT32_MAX) { fputs("STOP: module reference overflow\n",stderr); exit(21); }
    ++module_refs[id];
    if (id==0xb4) { refs=module_refs[id]; printf("Runtime: AppContent host provider loaded; references=%u\n",refs); }
    else printf("Runtime: system module 0x%x loaded (host implementation)\n",id);
    pthread_mutex_unlock(&lock);
    return 0;
}
static ABI int32_t module_loaded(uint16_t id) {
    if (!id) return INVALID_ID;
    pthread_mutex_lock(&lock);
    const int32_t result=module_refs[id] ? 0 : NOT_LOADED;
    pthread_mutex_unlock(&lock);
    return result;
}
static ABI int32_t module_unload(uint16_t id) {
    if (!id) return INVALID_ID;
    pthread_mutex_lock(&lock);
    if (!module_refs[id]) { pthread_mutex_unlock(&lock); return NOT_LOADED; }
    --module_refs[id];
    if (id==0xb4) {
        refs=module_refs[id];
        if (!refs) {
            /* Final AppContent reference: drop every still-active add-on mount here, under the
               content lock, so no /addcontN path or slot survives an unloaded provider. Open
               guest descriptors keep their host fd (POSIX): only the path mapping goes away. */
            for (unsigned i=0;i<ADDON_SLOTS;++i) {
                if (!addons[i].used) continue;
                runtime_file_unmount(addons[i].guest);
                memset(&addons[i],0,sizeof(addons[i]));
            }
            addons_mounted=0;
            initialized=0;
        }
    }
    pthread_mutex_unlock(&lock);
    return 0;
}
static void require_loaded(void) {
    require_provider();
    if (!refs) { fputs("STOP: AppContent called before module load\n",stderr); exit(21); }
}
static ABI int32_t content_init(const unsigned char *init,unsigned char *boot) {
    pthread_mutex_lock(&lock);
    require_loaded();
    if (!init || !boot || initialized) {
        pthread_mutex_unlock(&lock);
        return (!init || !boot) ? PARAMETER : BUSY;
    }
    /* Only the reserved-zero init profile is implemented. */
    for (unsigned i=0;i<32;++i) if (init[i]) { pthread_mutex_unlock(&lock); return PARAMETER; }
    memset(boot,0,40); initialized=1;
    puts("Runtime: AppContent initialized; offline base-game profile, add-on mounts follow this title");
    pthread_mutex_unlock(&lock);
    return 0;
}
static void require_initialized(void) {
    require_loaded();
    if (!initialized) { fputs("STOP: AppContent query before initialization\n",stderr); exit(21); }
}
static ABI int32_t param_int(uint32_t id,int32_t *out) {
    pthread_mutex_lock(&lock);
    require_initialized();
    if (id>4 || !out) { pthread_mutex_unlock(&lock); return PARAMETER; }
    memcpy(out,&parameters[id],4); ++queries;
    printf("Runtime: AppContent parameter %u = %d\n",id,*out);
    pthread_mutex_unlock(&lock);
    return 0;
}
/* SceAppContentAddcontInfo: entitlement label (16 chars + NUL), padding, download status. */
#define ADDON_INSTALLED 4u
typedef struct { char label[LABEL_SIZE]; char padding[3]; uint32_t status; } AddonInfo;
/* Add-ons are the folders <user>/addcont/<current title>/<entitlement label> (shadPS4's
 * layout), each reported as installed. No add-on data is downloaded; Bloodborne: The Old Hunters
 * (SPEXPANSIONDLC03) needs only the entitlement, its data ships with patch 1.09. */
static int valid_component(const char *name,size_t max) {
    if (!name || !*name) return 0;
    const size_t n=strlen(name);
    if (n>=max || name[0]=='.') return 0;
    for (size_t i=0;i<n;++i) {
        const unsigned char c=(unsigned char)name[i];
        if (c=='/' || c=='\\' || c<0x20 || c==0x7f) return 0;
    }
    return 1;
}
static int valid_label(const char *label) { return valid_component(label,LABEL_SIZE); }
static int valid_label_arg(const char *label) {
    return label && strnlen(label,LABEL_SIZE) < LABEL_SIZE && valid_label(label);
}
void runtime_content_set_title(const char *value) {
    char next[sizeof(title)]={0};
    if (value && valid_component(value,sizeof(title))) snprintf(next,sizeof(next),"%s",value);
    pthread_mutex_lock(&lock);
    if (addons_mounted) {
        fprintf(stderr,"Runtime: AppContent title change refused while add-ons are mounted\n");
    } else {
        memcpy(title,next,sizeof(title));
    }
    pthread_mutex_unlock(&lock);
}
static int under_user(const char *root_real) {
    char user_real[PATH_MAX];
    if (!realpath(runtime_file_user_dir(),user_real)) return 0;
    const size_t n=strlen(user_real);
    return !strncmp(root_real,user_real,n) && (root_real[n]=='/' || root_real[n]==0);
}
/* A real directory under <user>/addcont/<title>, never a symlink at the addcont/title/label
 * components and never outside the user root. Returns the canonical host path. */
static int addon_paths(const char *label,char *out,size_t size) {
    if (!title[0] || !valid_component(title,sizeof(title)) || !valid_label(label)) return 0;
    char root[PATH_MAX], candidate[PATH_MAX], root_real[PATH_MAX], resolved[PATH_MAX];
    if (snprintf(root,sizeof(root),"%s/addcont",runtime_file_user_dir())>= (int)sizeof(root) ||
        snprintf(candidate,sizeof(candidate),"%s/%s/%s",root,title,label)>=(int)sizeof(candidate)) return 0;
    const int rfd=open(root,O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if (rfd<0) return 0;
    const int tfd=openat(rfd,title,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    close(rfd);
    if (tfd<0) return 0;
    const int lfd=openat(tfd,label,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    close(tfd);
    if (lfd<0) return 0;
    struct stat st;
    const int regular=!fstat(lfd,&st) && S_ISDIR(st.st_mode);
    close(lfd);
    if (!regular || !realpath(root,root_real) || !under_user(root_real) ||
        !realpath(candidate,resolved)) return 0;
    const size_t n=strlen(root_real);
    if (strncmp(resolved,root_real,n) || resolved[n]!='/') return 0;
    if (snprintf(out,size,"%s",resolved)>=(int)size) return 0;
    return 1;
}
static DIR *open_title_dir(void) {
    if (!title[0] || !valid_component(title,sizeof(title))) return NULL;
    char root[PATH_MAX], root_real[PATH_MAX];
    if (snprintf(root,sizeof(root),"%s/addcont",runtime_file_user_dir())>=(int)sizeof(root) ||
        !realpath(root,root_real) || !under_user(root_real)) return NULL;
    const int rfd=open(root_real,O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if (rfd<0) return NULL;
    const int tfd=openat(rfd,title,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    close(rfd);
    if (tfd<0) return NULL;
    DIR *d=fdopendir(tfd);
    if (!d) close(tfd);
    return d;
}
static int label_compare(const void *a,const void *b) {
    return strcmp(*(char *const *)a,*(char *const *)b);
}
/* Without a list the count is returned; with one at most capacity entries are written. Slots
 * after the written entries are untouched. */
static ABI int32_t addon_list(uint32_t service,void *list,uint32_t capacity,uint32_t *hits) {
    pthread_mutex_lock(&lock);
    require_initialized();
    if (service) { pthread_mutex_unlock(&lock); return PARAMETER; }
    if ((!capacity || !list) && !hits) { pthread_mutex_unlock(&lock); return PARAMETER; }
    char **labels=NULL;
    size_t count=0, cap=0;
    DIR *titles=open_title_dir();
    for (struct dirent *e; titles && (e=readdir(titles));) {
        if (e->d_name[0]=='.' || !valid_label(e->d_name)) continue;
        struct stat st;
        if (fstatat(dirfd(titles),e->d_name,&st,AT_SYMLINK_NOFOLLOW) || !S_ISDIR(st.st_mode)) continue;
        if (count==cap) {
            const size_t next=cap ? cap*2 : 16;
            char **grown=realloc(labels,next*sizeof(*labels));
            if (!grown) { if (titles) closedir(titles); for (size_t i=0;i<count;++i) free(labels[i]); free(labels); pthread_mutex_unlock(&lock); return PARAMETER; }
            labels=grown; cap=next;
        }
        labels[count]=strdup(e->d_name);
        if (!labels[count]) { if (titles) closedir(titles); for (size_t i=0;i<count;++i) free(labels[i]); free(labels); pthread_mutex_unlock(&lock); return PARAMETER; }
        ++count;
    }
    if (titles) closedir(titles);
    if (count>1) qsort(labels,count,sizeof(*labels),label_compare);
    const uint32_t written=list && capacity ? (uint32_t)(count<capacity ? count : capacity) : 0;
    for (uint32_t i=0;i<written;++i) {
        AddonInfo info;
        memset(&info,0,sizeof(info));
        snprintf(info.label,sizeof(info.label),"%s",labels[i]);
        info.status=ADDON_INSTALLED;
        memcpy((unsigned char *)list+(size_t)i*sizeof(info),&info,sizeof(info));
    }
    for (size_t i=0;i<count;++i) free(labels[i]);
    free(labels);
    if (hits) *hits=(uint32_t)count;
    if (count) printf("Runtime: AppContent add-ons reported installed (%s/%s): %u\n",runtime_file_user_dir(),title,(unsigned)count);
    ++lists;
    pthread_mutex_unlock(&lock);
    return 0;
}
#define ADDON_NOT_FOUND ((int32_t)0x80d90005)
static int parse_point(const char *point,int *slot) {
    if (!point || strncmp(point,"/addcont",8)) return 0;
    char *end=NULL;
    const long value=strtol(point+8,&end,10);
    if (end==point+8 || *end || value<0 || value>=ADDON_SLOTS) return 0;
    char expected[16];
    snprintf(expected,sizeof(expected),"/addcont%ld",value);
    if (strcmp(point,expected)) return 0;
    *slot=(int)value;
    return 1;
}
/* sceAppContentAddcontMount: the add-on's files appear as /addcont0..15 (read-only data). The
 * slot and output are published only after the host mount table accepted the mount. A repeated
 * mount returns the same point and adds a reference; unmount drops one at a time. */
static ABI int32_t addon_mount(uint32_t service,const char *label,char *point) {
    pthread_mutex_lock(&lock);
    require_initialized();
    if (service || !label || !point) { pthread_mutex_unlock(&lock); return PARAMETER; }
    if (!valid_label_arg(label)) { pthread_mutex_unlock(&lock); return PARAMETER; }
    char name[LABEL_SIZE]={0};
    snprintf(name,sizeof(name),"%s",label);
    for (unsigned i=0;i<ADDON_SLOTS;++i) {
        if (addons[i].used && !strcmp(addons[i].label,name)) {
            ++addons[i].refs;
            if (addons[i].refs==0) { --addons[i].refs; pthread_mutex_unlock(&lock); return BUSY; }
            memcpy(point,addons[i].guest,16);
            pthread_mutex_unlock(&lock);
            return 0;
        }
    }
    int slot=-1;
    for (unsigned i=0;i<ADDON_SLOTS;++i) if (!addons[i].used) { slot=(int)i; break; }
    if (slot<0) { pthread_mutex_unlock(&lock); return BUSY; }
    char host[PATH_MAX];
    if (!addon_paths(name,host,sizeof(host))) {
        printf("Runtime: add-on '%s' not installed for title %s\n",name,title[0]?title:"(unset)");
        pthread_mutex_unlock(&lock);
        return ADDON_NOT_FOUND;
    }
    char guest[16];
    snprintf(guest,sizeof(guest),"/addcont%d",slot);
    if (runtime_file_mount(guest,host)) { pthread_mutex_unlock(&lock); return BUSY; }
    addons[slot].used=1; addons[slot].refs=1;
    snprintf(addons[slot].label,sizeof(addons[slot].label),"%s",name);
    snprintf(addons[slot].guest,sizeof(addons[slot].guest),"%s",guest);
    ++addons_mounted;
    memset(point,0,16);
    memcpy(point,guest,strlen(guest));
    printf("Runtime: add-on '%s' mounted at %s (%s)\n",name,guest,host);
    pthread_mutex_unlock(&lock);
    return 0;
}
static ABI int32_t addon_unmount(const char *point) {
    int slot=0;
    if (!parse_point(point,&slot)) return PARAMETER;
    pthread_mutex_lock(&lock);
    require_initialized();
    if (!addons[slot].used) { pthread_mutex_unlock(&lock); return ADDON_NOT_FOUND; }
    if (--addons[slot].refs) { pthread_mutex_unlock(&lock); return 0; }
    runtime_file_unmount(addons[slot].guest);
    memset(&addons[slot],0,sizeof(addons[slot]));
    --addons_mounted;
    pthread_mutex_unlock(&lock);
    return 0;
}
/* sceAppContentGetAddcontInfo: an installed add-on of the current title. */
static ABI int32_t addon_info(uint32_t service,const char *label,void *info) {
    pthread_mutex_lock(&lock);
    require_initialized();
    if (service || !label || !info) { pthread_mutex_unlock(&lock); return PARAMETER; }
    if (!valid_label_arg(label)) { pthread_mutex_unlock(&lock); return PARAMETER; }
    char name[LABEL_SIZE]={0}, host[PATH_MAX];
    snprintf(name,sizeof(name),"%s",label);
    if (!addon_paths(name,host,sizeof(host))) {
        pthread_mutex_unlock(&lock); return ADDON_NOT_FOUND;
    }
    AddonInfo out;
    memset(&out,0,sizeof(out));
    snprintf(out.label,sizeof(out.label),"%s",name);
    out.status=ADDON_INSTALLED;
    memcpy(info,&out,sizeof(out));
    pthread_mutex_unlock(&lock);
    return 0;
}
uintptr_t runtime_content_resolve(const char *name) {
    if (runtime_nid_is(name, "VANhIWcqYak")) return (uintptr_t)addon_mount;
    if (runtime_nid_is(name, "3rHWaV-1KC4")) return (uintptr_t)addon_unmount;
    if (runtime_nid_is(name, "m47juOmH0VE")) return (uintptr_t)addon_info;
    if (runtime_nid_is(name, "g8cM39EUZ6o")) return (uintptr_t)module_load;
    if (runtime_nid_is(name, "fMP5NHUOaMk")) return (uintptr_t)module_loaded;
    if (runtime_nid_is(name, "eR2bZFAAU0Q")) return (uintptr_t)module_unload;
    if (runtime_nid_is(name, "R9lA82OraNs")) return (uintptr_t)content_init;
    if (runtime_nid_is(name, "99b82IKXpH4")) return (uintptr_t)param_int;
    if (runtime_nid_is(name, "xnd8BJzAxmk")) return (uintptr_t)addon_list;
    return 0;
}
void runtime_content_report(void) {
    pthread_mutex_lock(&lock);
    printf("Runtime: AppContent references=%u, initialized=%u, parameter queries=%u, list queries=%u, mounted add-ons=%u\n",refs,initialized,queries,lists,addons_mounted);
    pthread_mutex_unlock(&lock);
}
