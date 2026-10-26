/* Director ink 35 needs destination minus source. vita2d exposes only alpha and
   addition, so retain an extra GXM variant while its fragment programs are built. */
#include <vita2d.h>
#include "gfx.h"
#include "battle_blend.h"
static struct {const SceGxmFragmentProgram *original;SceGxmFragmentProgram *subtract;} programs[16];
static unsigned count;
static int mode;
static bool failed;
int __real_sceGxmShaderPatcherCreateFragmentProgram(SceGxmShaderPatcher *,SceGxmShaderPatcherId,SceGxmOutputRegisterFormat,SceGxmMultisampleMode,const SceGxmBlendInfo *,const SceGxmProgram *,SceGxmFragmentProgram **);
void __real_sceGxmSetFragmentProgram(SceGxmContext *,const SceGxmFragmentProgram *);
int __wrap_sceGxmShaderPatcherCreateFragmentProgram(SceGxmShaderPatcher *patcher,SceGxmShaderPatcherId id,SceGxmOutputRegisterFormat format,SceGxmMultisampleMode samples,const SceGxmBlendInfo *blend,const SceGxmProgram *vertex,SceGxmFragmentProgram **out){
  int result=__real_sceGxmShaderPatcherCreateFragmentProgram(patcher,id,format,samples,blend,vertex,out);
  if(result<0||!blend)return result;
  if(count>=16){failed=true;return result;}
  SceGxmBlendInfo subtract=*blend;subtract.colorFunc=SCE_GXM_BLEND_FUNC_REVERSE_SUBTRACT;
  subtract.colorSrc=SCE_GXM_BLEND_FACTOR_SRC_ALPHA;subtract.colorDst=SCE_GXM_BLEND_FACTOR_ONE;
  subtract.alphaFunc=SCE_GXM_BLEND_FUNC_ADD;subtract.alphaSrc=SCE_GXM_BLEND_FACTOR_ZERO;subtract.alphaDst=SCE_GXM_BLEND_FACTOR_ONE;
  SceGxmFragmentProgram *program=NULL;
  if(__real_sceGxmShaderPatcherCreateFragmentProgram(patcher,id,format,samples,&subtract,vertex,&program)<0){failed=true;return result;}
  programs[count].original=*out;programs[count++].subtract=program;return result;
}
void __wrap_sceGxmSetFragmentProgram(SceGxmContext *context,const SceGxmFragmentProgram *program){
  if(mode==2)for(unsigned i=0;i<count;i++)if(programs[i].original==program){program=programs[i].subtract;break;}
  __real_sceGxmSetFragmentProgram(context,program);
}
void gfx_battle_blend(int value){mode=value;gfx_additive(value==1);}
bool battle_blend_ready(void){return count>0&&!failed;}
void battle_blend_free(void){while(count)sceGxmShaderPatcherReleaseFragmentProgram(vita2d_get_shader_patcher(),programs[--count].subtract);mode=0;}
