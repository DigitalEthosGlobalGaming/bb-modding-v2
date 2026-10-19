#include "bbmod.h"
static const bbm_host *host;
static bbm_handle self;
static bbm_status load(bbm_ctx *ctx) { (void)ctx; host->log(self, BBM_LOG_INFO, "discovery fixture loaded"); return BBM_OK; }
BBM_EXPORT bbm_status bbmod_entry(const bbm_host *h, bbm_handle m, bbm_mod_api *api, bbm_ctx **ctx) {
 host=h;self=m;*ctx=0;
 *api=(bbm_mod_api){.size=sizeof *api,.abi_major=BBM_ABI_MAJOR,.abi_minor=BBM_ABI_MINOR,.on_load=load};
 return BBM_OK;
}
