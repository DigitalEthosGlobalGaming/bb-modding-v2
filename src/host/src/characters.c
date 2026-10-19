/* SPDX-License-Identifier: GPL-2.0-or-later
 * Independent bounded reader. Layout leads: bbhost engine sprj headers;
 * native instructions checked in discoveries/character-profile.json. */
#include "characters.h"
#include <string.h>
#include <stdlib.h>
#include "character_profile.h"
static int rd(mh_character_reader r,void*u,uintptr_t p,size_t off,void*out,size_t n){
    return p<0x10000||off>UINTPTR_MAX-p||n>UINTPTR_MAX-(p+off)||r(u,p+off,out,n);
}
static int ptr(mh_character_reader r,void*u,uintptr_t p,size_t off,uintptr_t*out){
    return rd(r,u,p,off,out,8)||*out<0x10000||(*out&7);
}
int mh_characters_profile(uintptr_t base,mh_character_reader r,void*u){
    for(size_t i=0;i<sizeof character_checks/sizeof character_checks[0];i++){
        unsigned char b[32];const struct character_check*c=&character_checks[i];
        if(rd(r,u,base,c->rva,b,c->n)||memcmp(b,c->bytes,c->n))return 0;
    }return 1;
}
static int one(uintptr_t base,uintptr_t actor,uintptr_t slot,uint32_t selector,uint32_t index,uintptr_t player,uintptr_t player_vt,
               mh_character_reader r,void*u,mh_character*out){
    uintptr_t vt,modules,data,owner,again,entry;
    bbm_character*v=&out->value;memset(out,0,sizeof *out);v->size=sizeof *v;v->npc_param=v->name_id=-1;
    if(ptr(r,u,actor,0,&vt)||rd(r,u,actor,8,&v->handle,4)||((v->handle>>14)&63)!=selector||(v->handle&0x3fff)!=index||
       ptr(r,u,actor,0x18,&entry)||entry!=slot||ptr(r,u,actor,0x3b0,&modules)||ptr(r,u,modules,0x20,&data)||
       ptr(r,u,data,8,&owner)||owner!=actor||rd(r,u,actor,0x88,&v->team_type,4)||rd(r,u,actor,0xe4,&v->update_interval,4)||
       rd(r,u,slot,0x14,&v->backread_state,4))return -1;
    v->kind=actor==player?1:vt==player_vt?2:vt==base+0x538ebc0?3:0;
    if(!rd(r,u,data,0xf8,&v->health,8)&&v->max_health>0&&v->max_health<=100000000&&v->health>=0&&v->health<=v->max_health)
        v->flags|=BBM_CHARACTER_HEALTH;
    else v->health=v->max_health=0;
    if(v->kind==3){uintptr_t ref,row;
        if(!ptr(r,u,actor,0x3c8,&ref)&&!rd(r,u,ref,0,&v->npc_param,4)&&v->npc_param>=0&&
           !ptr(r,u,ref,8,&row)&&!rd(r,u,row,8,&v->name_id,4)&&!ptr(r,u,actor,0x3c8,&again)&&again==ref)
            v->flags|=BBM_CHARACTER_NPC_PARAM;
        else v->npc_param=v->name_id=-1;
    }
    uintptr_t module,mvt;
    if(!ptr(r,u,modules,0x40,&module)&&!ptr(r,u,module,0,&mvt)&&mvt==base+0x53357e0&&
       !ptr(r,u,module,8,&owner)&&owner==actor)v->flags|=BBM_CHARACTER_AI_MODULE;
    if(!ptr(r,u,modules,0x58,&module)&&!ptr(r,u,module,0,&mvt)&&mvt==base+0x53358b0&&
       !ptr(r,u,module,8,&owner)&&owner==actor&&!rd(r,u,module,0x20,&v->script_value,4))v->flags|=BBM_CHARACTER_SCRIPT_VALUE;
    uint32_t handle;
    if(ptr(r,u,slot,0,&again)||again!=actor||rd(r,u,actor,8,&handle,4)||handle!=v->handle||
       ptr(r,u,actor,0x3b0,&again)||again!=modules||ptr(r,u,modules,0x20,&again)||again!=data)return -1;
    v->flags|=BBM_CHARACTER_AVAILABLE;out->actor=actor;out->data=data;return 0;
}
bbm_status mh_characters_read(uintptr_t base,mh_character_reader r,void*u,mh_character_sample*out){
    memset(out,0,sizeof *out);uintptr_t world,player,pvt,info,sets[62],verify[62],again;int32_t blocks;
    if(ptr(r,u,base,0x553e878,&world)||ptr(r,u,world,0x60,&player)||ptr(r,u,player,0,&pvt)||
       ptr(r,u,world,8,&info)||rd(r,u,info,0x18,&blocks,4)||blocks<0||blocks>60||rd(r,u,world,0x850,sets,sizeof sets))return BBM_ERR_NOTFOUND;
    uint32_t visited=0;bbm_status status=BBM_ERR_NOTFOUND;
    /* Temporary slot storage is bounded to 4096 entries, not an address-space scan. */
    unsigned char*slots=malloc(4096*0x38);if(!slots)return BBM_ERR_NOMEM;
    for(uint32_t s=0;s<(uint32_t)blocks+2;s++){
        if(!sets[s])continue;
        uint32_t count,count2;uintptr_t entries;
        if(rd(r,u,sets[s],0,&count,4))goto done;
        if(count>4096||visited>8192-count){status=BBM_ERR_BUDGET;goto done;}visited+=count;
        if(!count)continue;
        if(ptr(r,u,sets[s],8,&entries)||rd(r,u,entries,0,slots,(size_t)count*0x38))goto done;
        for(uint32_t i=0;i<count;i++){
            uintptr_t actor;memcpy(&actor,slots+(size_t)i*0x38,8);if(!actor)continue;
            if(out->count==512){status=BBM_ERR_BUDGET;goto done;}
            if(one(base,actor,entries+(size_t)i*0x38,s,i,player,pvt,r,u,&out->entries[out->count]))goto done;
            for(uint32_t j=0;j<out->count;j++)if(out->entries[j].actor==actor)goto done;
            out->count++;
        }
        if(rd(r,u,sets[s],0,&count2,4)||count!=count2||ptr(r,u,sets[s],8,&again)||again!=entries)goto done;
        for(uint32_t i=0;i<count;i++){uintptr_t actor;memcpy(&actor,slots+(size_t)i*0x38,8);
            if(rd(r,u,entries,(size_t)i*0x38,&again,8)||again!=actor)goto done;}
    }
    if(rd(r,u,world,0x850,verify,sizeof verify)||memcmp(sets,verify,sizeof sets)||ptr(r,u,base,0x553e878,&again)||again!=world||
       ptr(r,u,world,0x60,&again)||again!=player||ptr(r,u,world,8,&again)||again!=info)goto done;
    int32_t blocks2;if(rd(r,u,info,0x18,&blocks2,4)||blocks2!=blocks)goto done;
    out->world=world;out->player=player;status=BBM_OK;
done:free(slots);if(status)memset(out,0,sizeof *out);return status;
}
