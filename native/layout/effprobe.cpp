#include "stdafx.h"
#include "DxEffAni.h"
#include "DxEffAniSingle.h"
#include "DxEffChar.h"
#include "DxEffCharAlpha.h"
#include "DxEffCharAmbient.h"
#include "DxEffCharBlur.h"
#include "DxEffCharBoneListEff.h"
#include "DxEffCharGhosting.h"
#include "DxEffCharLevel.h"
#include "DxEffCharLine2BoneEff.h"
#include "DxEffCharMultiTex.h"
#include "DxEffCharNeon.h"
#include "DxEffCharNoAlpha.h"
#include "DxEffCharParticle.h"
#include "DxEffCharReflection2.h"
#include "DxEffCharShock.h"
#include "DxEffCharSingle.h"
#include "DxEffCharSpecular2.h"
#include "DxEffCharTexDiff.h"
#include "DxEffCharUserColor.h"
#include "DxEffKeep.h"
#include "DxEffSinglePropGMan.h"
#include "DxEffectCloth.h"
#include "DxEffectDot3.h"
#include "DxEffectFur.h"
#include "DxEffectGlow.h"
#include "DxEffectGrass.h"
#include "DxEffectLightMap.h"
#include "DxEffectMultiTex.h"
#include "DxEffectNature.h"
#include "DxEffectNeon.h"
#include "DxEffectRainDrop.h"
#include "DxEffectReflect.h"
#include "DxEffectRenderState.h"
#include "DxEffectRiver.h"
#include "DxEffectShadowLow.h"
#include "DxEffectSpecReflect.h"
#include "DxEffectSpecular.h"
#include "DxEffectSpecular2.h"
#include "DxEffectSpore.h"
#include "DxEffectTiling.h"
#include "DxEffectToon.h"
#include "DxEffectWater.h"
#include "DxEffectWater2.h"
#include "DxEffectWaterLight.h"
#include "DxMapEditMan.h"
#include "DxTexEffDiffuse.h"
#include "DxTexEffFlowUV.h"
#include "DxTexEffRotate.h"
#include "DxTexEffSpecular.h"
#include "DxTexEffVisualMaterial.h"
#include "DxTextureEffMan.h"

template <class T> struct RanSize { static const size_t value = sizeof(T); };
size_t ran_layout_probe_total = 0;
void ran_layout_probe() {
    ran_layout_probe_total += RanSize<CARTOON_PROPERTY>::value;
    ran_layout_probe_total += RanSize<CLOTH_PROPERTY_100>::value;
    ran_layout_probe_total += RanSize<CLOTH_PROPERTY_101>::value;
    ran_layout_probe_total += RanSize<CLOTH_PROPERTY_102_103>::value;
    ran_layout_probe_total += RanSize<DOT3_PROPERTY>::value;
    ran_layout_probe_total += RanSize<EFFANI_PROPERTY>::value;
    ran_layout_probe_total += RanSize<EFFANI_PROPERTY_SINGLE_100>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_ALPHA_100>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_ALPHA_101>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_ALPHA_102>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_AMBIENT_100>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_AMBIENT_101>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_AMBIENT_102>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_AMBIENT_103>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_BLUR_100>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_BLUR_101>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_BLUR_102>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_BLUR_103>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_BLUR_104>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_BLUR_105>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_BONELISTEFF_100>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_GHOSTING_100>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_LEVEL_100>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_LEVEL_101>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_LEVEL_102_103>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_LEVEL_104_105>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_LEVEL_106>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_LINE2BONEEFF_0100>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_LINE2BONEEFF_0101>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_LINE2BONEEFF_0102>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_MULTITEX_100_1>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_MULTITEX_102>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_NEON_100>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_NEON_101>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_NOALPHA_100>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_NOALPHA_101>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_PARTICLE_100>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_PARTICLE_101>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_REFLECTION2_100>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_REFLECTION2_101>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_SHOCK_100>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_SHOCK_101>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_SINGLE_100>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_SINGLE_101>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_SINGLE_102>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_SINGLE_103>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_SINGLE_104>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_SINGLE_105>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_SINGLE_106>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_SINGLE_107>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_SINGLE_108>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_SPECULAR2_104_105>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_TEXDIFF_100>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_TEXDIFF_101>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_TEXDIFF_102>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_USERCOLOR_100>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_USERCOLOR_101>::value;
    ran_layout_probe_total += RanSize<EFFKEEP_PROPERTY>::value;
    ran_layout_probe_total += RanSize<EFF_PROPERTY>::value;
    ran_layout_probe_total += RanSize<FUR_PROPERTY>::value;
    ran_layout_probe_total += RanSize<GLOW_PROPERTY>::value;
    ran_layout_probe_total += RanSize<GRASS_PROPERTY>::value;
    ran_layout_probe_total += RanSize<LIGHTMAP_PROPERTY>::value;
    ran_layout_probe_total += RanSize<MAPEDIT_PROPERTY>::value;
    ran_layout_probe_total += RanSize<MULTITEX_PROPERTY>::value;
    ran_layout_probe_total += RanSize<MULTITEX_PROPERTY_100>::value;
    ran_layout_probe_total += RanSize<NATURE_PROPERTY>::value;
    ran_layout_probe_total += RanSize<NATURE_PROPERTY_100>::value;
    ran_layout_probe_total += RanSize<NEON_PROPERTY>::value;
    ran_layout_probe_total += RanSize<RAINPOINT_PROPERTY_100>::value;
    ran_layout_probe_total += RanSize<RAINPOINT_PROPERTY_101>::value;
    ran_layout_probe_total += RanSize<RAINPOINT_PROPERTY_102>::value;
    ran_layout_probe_total += RanSize<REFLECT_PROPERTY>::value;
    ran_layout_probe_total += RanSize<RENDERSTATE_PROPERTY>::value;
    ran_layout_probe_total += RanSize<RENDERSTATE_PROPERTY_100>::value;
    ran_layout_probe_total += RanSize<RENDERSTATE_PROPERTY_101>::value;
    ran_layout_probe_total += RanSize<RENDERSTATE_PROPERTY_102>::value;
    ran_layout_probe_total += RanSize<RIVER_PROPERTY_100>::value;
    ran_layout_probe_total += RanSize<SHADOW_PROPERTY_L_100>::value;
    ran_layout_probe_total += RanSize<SPECREFLECT_PROPERTY>::value;
    ran_layout_probe_total += RanSize<SPECULAR2_PROPERTY>::value;
    ran_layout_probe_total += RanSize<SPECULAR_PROPERTY>::value;
    ran_layout_probe_total += RanSize<SPORE_PROPERTY>::value;
    ran_layout_probe_total += RanSize<SPORE_PROPERTY_100>::value;
    ran_layout_probe_total += RanSize<TEXEFF_DIFFUSE_PROPERTY>::value;
    ran_layout_probe_total += RanSize<TEXEFF_DIFFUSE_PROPERTY_100>::value;
    ran_layout_probe_total += RanSize<TEXEFF_FLOWUV_PROPERTY>::value;
    ran_layout_probe_total += RanSize<TEXEFF_PROPERTY>::value;
    ran_layout_probe_total += RanSize<TEXEFF_ROTATE_PROPERTY>::value;
    ran_layout_probe_total += RanSize<TEXEFF_SPECULAR_PROPERTY>::value;
    ran_layout_probe_total += RanSize<TEXEFF_VISUALMATERIAL_PROPERTY>::value;
    ran_layout_probe_total += RanSize<TILING_PROPERTY_103>::value;
    ran_layout_probe_total += RanSize<WATER2_PROPERTY>::value;
    ran_layout_probe_total += RanSize<WATERLIGHT_PROPERTY>::value;
    ran_layout_probe_total += RanSize<WATERLIGHT_PROPERTY_100>::value;
    ran_layout_probe_total += RanSize<WATER_PROPERTY_100>::value;
    ran_layout_probe_total += RanSize<WATER_PROPERTY_101>::value;
}
