#include "tcompat.h"
#include "internal.h"
#include <assert.h>
static int loads;
static void log_line(void*u,const char*id,int level,const char*msg){(void)u;(void)id;(void)level;if(!strcmp(msg,"discovery fixture loaded"))loads++;}
static void due(modhost*h){h->last_discovery_ms=plat_now_ms()-5001;modhost_frame(h);}
int main(int argc,char**argv){
 assert(argc==2);char root[300],mods[400],cache[400],path[600],dest[600];
 tc_tmpdir(root,sizeof root,"bbmod-discovery");snprintf(mods,sizeof mods,"%s/mods",root);snprintf(cache,sizeof cache,"%s/cache",root);tc_mkdirs(mods);
 modhost_config c={0};c.mods_dir=mods;c.cache_dir=cache;c.log=log_line;
 modhost*h=modhost_create(&c);assert(modhost_scan(h)==0);assert(!h->cfg.dev_mode);
 tc_make_mod(mods,"new",argv[1],"[mod]\nid = \"new\"\ndll = \"m.dll\"\n");
 modhost_frame(h);assert(h->nmods==0);due(h);assert(loads==1&&mh_find(h,"new")->state==MODHOST_ACTIVE);
 due(h);assert(loads==1);assert(modhost_unload_now(h,"new")==0);due(h);assert(loads==1&&mh_find(h,"new")->state==MODHOST_UNLOADED);
 tc_make_mod(mods,"duplicate",argv[1],"[mod]\nid = \"new\"\ndll = \"m.dll\"\n");due(h);assert(h->nmods==1&&loads==1);
 snprintf(path,sizeof path,"%s/late",mods);tc_mkdirs(path);snprintf(path,sizeof path,"%s/late/mod.toml",mods);tc_write(path,"[mod]\nid = \"late\"\ndll = \"m.dll\"\n");
 due(h);assert(!mh_find(h,"late"));snprintf(dest,sizeof dest,"%s/late/m.dll",mods);assert(tc_copy(argv[1],dest)==0);due(h);assert(loads==2&&mh_find(h,"late")->inst);
 /* Force realloc and more than the previous 256-entry sort buffer. */
 for(int i=0;i<270;i++){snprintf(path,sizeof path,"%s/asset-%03d/dvdroot_ps4",mods,i);tc_mkdirs(path);}
 snprintf(path,sizeof path,"%s/asset-269/dvdroot_ps4/late.bin",mods);assert(tc_write(path,"asset")==0);
 due(h);assert(h->nmods==272);assert(mh_find(h,"late")->inst->entry==mh_find(h,"late"));assert(loads==2);
 char resolved[1024];assert(modhost_vfs_resolve(h,"dvdroot_ps4/late.bin",resolved,sizeof resolved)==1);
 assert(modhost_unload_now(h,"asset-269")==0);due(h);assert(modhost_vfs_resolve(h,"dvdroot_ps4/late.bin",resolved,sizeof resolved)==0);
 assert(modhost_load_now(h,"asset-269")==0);assert(modhost_vfs_resolve(h,"dvdroot_ps4/late.bin",resolved,sizeof resolved)==1);
 mh_mod_faulted(mh_find(h,"late")->inst,"test");modhost_frame(h);due(h);assert(loads==2&&mh_find(h,"late")->state==MODHOST_FAULTED);
 c.safe_mode=1;modhost_destroy(h);h=modhost_create(&c);modhost_scan(h);assert(loads==2);assert(modhost_vfs_resolve(h,"dvdroot_ps4/late.bin",resolved,sizeof resolved)==0);
 tc_make_mod(mods,"safe",argv[1],"[mod]\nid = \"safe\"\ndll = \"m.dll\"\n");due(h);assert(mh_find(h,"safe")&&mh_find(h,"safe")->state!=MODHOST_ACTIVE&&loads==2);
 modhost_destroy(h);tc_rmtree(root);puts("periodic discovery: cadence, new DLLs, safe mode, duplicate IDs, unload/fault preservation and growth OK");return 0;
}
