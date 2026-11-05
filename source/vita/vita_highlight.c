#include "vita_highlight.h"
#include "highlight_gxp.h"
#include <stddef.h>
#include <string.h>
extern const SceGxmProgram texture_v_gxp_start;
static SceGxmShaderPatcherId vertex_id,fragment_id[2];
static bool vertex_registered,fragment_registered[2];
static SceGxmVertexProgram *vertex;
static SceGxmFragmentProgram *fragment[2];
static const SceGxmProgramParameter *wvp,*uniforms[2][6];
static const float ortho[16]={2.0f/960,0,0,0,0,-2.0f/544,0,0,0,0,-2,0,-1,1,1,1};
void vita_highlight_free(void){
 SceGxmShaderPatcher *p=vita2d_get_shader_patcher();
 for(int i=0;i<2;i++){
  if(fragment[i])sceGxmShaderPatcherReleaseFragmentProgram(p,fragment[i]);
  if(fragment_registered[i])sceGxmShaderPatcherUnregisterProgram(p,fragment_id[i]);
  fragment[i]=NULL;fragment_registered[i]=false;
 }
 if(vertex)sceGxmShaderPatcherReleaseVertexProgram(p,vertex);
 if(vertex_registered)sceGxmShaderPatcherUnregisterProgram(p,vertex_id);
 vertex=NULL;vertex_registered=false;
}
bool vita_highlight_init(void){
 SceGxmShaderPatcher *p=vita2d_get_shader_patcher();const SceGxmProgram *vp=&texture_v_gxp_start;
 const SceGxmProgramParameter *pos=sceGxmProgramFindParameterByName(vp,"aPosition"),*uv=sceGxmProgramFindParameterByName(vp,"aTexcoord");wvp=sceGxmProgramFindParameterByName(vp,"wvp");
 if(!pos||!uv||!wvp)return false;
 if(sceGxmShaderPatcherRegisterProgram(p,vp,&vertex_id)<0)return false;
 vertex_registered=true;
 SceGxmVertexAttribute a[2]={{0}};
 a[0].streamIndex=0;a[0].offset=0;a[0].format=SCE_GXM_ATTRIBUTE_FORMAT_F32;a[0].componentCount=3;a[0].regIndex=sceGxmProgramParameterGetResourceIndex(pos);
 a[1].streamIndex=0;a[1].offset=12;a[1].format=SCE_GXM_ATTRIBUTE_FORMAT_F32;a[1].componentCount=2;a[1].regIndex=sceGxmProgramParameterGetResourceIndex(uv);
 SceGxmVertexStream stream={sizeof(vita2d_texture_vertex),SCE_GXM_INDEX_SOURCE_INDEX_16BIT};
 if(sceGxmShaderPatcherCreateVertexProgram(p,vertex_id,a,2,&stream,1,&vertex)<0)goto failed;
 const SceGxmProgram *programs[2]={(const SceGxmProgram*)highlight_fragment_gxp,(const SceGxmProgram*)highlight_add_fragment_gxp};
 const char *names[6]={"uTintColor","uColumn0","uColumn1","uColumn2","uOffset","uAlpha"};
 for(int i=0;i<2;i++){
  if(sceGxmShaderPatcherRegisterProgram(p,programs[i],&fragment_id[i])<0)goto failed;
  fragment_registered[i]=true;
  for(int j=0;j<6;j++){uniforms[i][j]=sceGxmProgramFindParameterByName(programs[i],names[j]);if(!uniforms[i][j])goto failed;}
  SceGxmBlendInfo blend={0};blend.colorMask=SCE_GXM_COLOR_MASK_ALL;blend.colorFunc=blend.alphaFunc=SCE_GXM_BLEND_FUNC_ADD;
  blend.colorSrc=blend.alphaSrc=i?SCE_GXM_BLEND_FACTOR_ONE:SCE_GXM_BLEND_FACTOR_SRC_ALPHA;
  blend.colorDst=blend.alphaDst=i?SCE_GXM_BLEND_FACTOR_ONE:SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  if(sceGxmShaderPatcherCreateFragmentProgram(p,fragment_id[i],SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,SCE_GXM_MULTISAMPLE_NONE,&blend,vp,&fragment[i])<0)goto failed;
 }
 return true;
failed:vita_highlight_free();return false;
}
bool vita_highlight_draw(vita2d_texture *texture,float x,float y,int sx,int sy,int sw,int sh,float scale_x,float scale_y,unsigned color,bool additive,const GfxHighlightMatrix *matrix){
 if(!vertex||!fragment[additive?1:0])return false;
 vita2d_texture_vertex *v=vita2d_pool_memalign(4*sizeof(*v),sizeof(*v));if(!v)return false;
 float width=vita2d_texture_get_width(texture),height=vita2d_texture_get_height(texture);
 for(int n=0;n<4;n++){int right=n&1,bottom=n>>1;v[n].x=x+(right?sw*scale_x:0);v[n].y=y+(bottom?sh*scale_y:0);v[n].z=.5f;v[n].u=(sx+(right?sw:0))/width;v[n].v=(sy+(bottom?sh:0))/height;}
 SceGxmContext *context=vita2d_get_context();int index=additive?1:0;
 sceGxmSetVertexProgram(context,vertex);sceGxmSetFragmentProgram(context,fragment[index]);
 void *buffer=NULL;sceGxmReserveVertexDefaultUniformBuffer(context,&buffer);sceGxmSetUniformDataF(buffer,wvp,0,16,ortho);
 float data[6][4]={{(color&255)/255.f,((color>>8)&255)/255.f,((color>>16)&255)/255.f,(color>>24)/255.f}};
 data[5][3]=1;
 for(int c=0;c<4;c++)for(int r=0;r<3;r++)data[c+1][r]=matrix->row[r][c];
 sceGxmReserveFragmentDefaultUniformBuffer(context,&buffer);for(int j=0;j<6;j++)sceGxmSetUniformDataF(buffer,uniforms[index][j],0,4,data[j]);
 sceGxmSetFragmentTexture(context,0,&texture->gxm_tex);sceGxmSetVertexStream(context,0,v);sceGxmDraw(context,SCE_GXM_PRIMITIVE_TRIANGLE_STRIP,SCE_GXM_INDEX_FORMAT_U16,vita2d_get_linear_indices(),4);return true;
}
