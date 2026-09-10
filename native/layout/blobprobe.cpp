#include "stdafx.h"
#include "CList.h"
#include "CharacterSound.h"
#include "DxAniKeys.h"
#include "DxCubeMap.h"
#include "DxEffAniSingle.h"
#include "DxEffCharAlpha.h"
#include "DxEffCharAmbient.h"
#include "DxEffCharArrow.h"
#include "DxEffCharBlur.h"
#include "DxEffCharBoneListEff.h"
#include "DxEffCharCloneBlur.h"
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
#include "DxEffectBlurSys.h"
#include "DxEffectGlow.h"
#include "DxEffectGrass.h"
#include "DxEffectMoveRotate.h"
#include "DxEffectNeon.h"
#include "DxEffectRainDrop.h"
#include "DxEffectRiver.h"
#include "DxEffectSpecReflect.h"
#include "DxEffectSpecular2.h"
#include "DxEffectWaterLight.h"
#include "DxFrameMesh.h"
#include "DxGlowMan.h"
#include "DxLandDef.h"
#include "DxLandGateMan.h"
#include "DxMapBlend.h"
#include "DxMaterial.h"
#include "DxMethods.h"
#include "DxOctreeMesh.h"
#include "DxSkinCharData.h"
#include "DxVertexFVF.h"
#include "DxWeatherMan.h"
#include "GLActivityData.h"
#include "GLCharData.h"
#include "GLCodexData.h"
#include "GLCrowDataAction.h"
#include "GLCrowDataAttack.h"
#include "GLCrowDataBasic.h"
#include "GLDefine.h"
#include "GLFactData.h"
#include "GLItem.h"
#include "GLItemBox.h"
#include "GLItemDef.h"
#include "GLItemPetSkin.h"
#include "GLItemRandomBox.h"
#include "GLItemSelfBuff.h"
#include "GLLevelHead.h"
#include "GLMobSchedule.h"
#include "NpcTalkData.h"
#include "NsOCTree.h"
#include "SAnimationInfo.h"
#include "d3dfont.h"
#include "d3dx9math.h"
#include "line2d.h"
#include "navigationmesh.h"
#include "plane.h"

template <class T> struct RanSize { static const size_t value = sizeof(T); };
size_t ran_layout_probe_total = 0;
void ran_layout_probe() {
    ran_layout_probe_total += RanSize<CLOUD_PROPERTY>::value;
    ran_layout_probe_total += RanSize<CLOUD_PROPERTY_100>::value;
    ran_layout_probe_total += RanSize<D3DEXMATERIAL>::value;
    ran_layout_probe_total += RanSize<D3DWATERVERTEX>::value;
    ran_layout_probe_total += RanSize<D3DXCOLOR>::value;
    ran_layout_probe_total += RanSize<D3DXMATRIX>::value;
    ran_layout_probe_total += RanSize<D3DXVECTOR2>::value;
    ran_layout_probe_total += RanSize<D3DXVECTOR3>::value;
    ran_layout_probe_total += RanSize<DxEffectWaterLight::DIFFVERTEX>::value;
    ran_layout_probe_total += RanSize<OBJOCTree::DIRECTPOINTCOLOR>::value;
    ran_layout_probe_total += RanSize<DXAFFINEPARTS>::value;
    ran_layout_probe_total += RanSize<DXMATERIAL_CHAR_EFF>::value;
    ran_layout_probe_total += RanSize<DXMATERIAL_CHAR_EFF_100>::value;
    ran_layout_probe_total += RanSize<DXMATERIAL_NEON>::value;
    ran_layout_probe_total += RanSize<DXMATERIAL_SPEC2>::value;
    ran_layout_probe_total += RanSize<DXMATERIAL_SPECREFLECT>::value;
    ran_layout_probe_total += RanSize<DXMATERIAL_SPECULAR>::value;
    ran_layout_probe_total += RanSize<DXOCMATERIAL>::value;
    ran_layout_probe_total += RanSize<DXUSERMATERIAL>::value;
    ran_layout_probe_total += RanSize<EFFANI_PROPERTY_SINGLE_100>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_ALPHA_100>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_ALPHA_101>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_ALPHA_102>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_AMBIENT_100>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_AMBIENT_101>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_AMBIENT_102>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_AMBIENT_103>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_ARROW_100>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_BLUR>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_BLUR_100>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_BLUR_101>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_BLUR_102>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_BLUR_103>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_BLUR_104>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_BLUR_105>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_BONELISTEFF_100>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_CLONEBLUR_0100>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_GHOSTING_100>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_LEVEL_100>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_LEVEL_101>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_LEVEL_102_103>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_LEVEL_104_105>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_LEVEL_106>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_LINE2BONEEFF_0101>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_LINE2BONEEFF_0102>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_MULTITEX_100_1>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_MULTITEX_102>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_MULTITEX_103_104_105>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_NEON_100>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_NEON_101>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_NEON_102_103_104_105>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_NOALPHA_100>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_NOALPHA_101>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_PARTICLE_101>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_REFLECTION2_100>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_REFLECTION2_101>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_REFLECTION2_102>::value;
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
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_SPECULAR2_106>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_TEXDIFF_100>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_TEXDIFF_101>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_TEXDIFF_102>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_USERCOLOR_100>::value;
    ran_layout_probe_total += RanSize<EFFCHAR_PROPERTY_USERCOLOR_101>::value;
    ran_layout_probe_total += RanSize<GLPANDORA_BOX>::value;
    ran_layout_probe_total += RanSize<DxEffectGrass::GRASS>::value;
    ran_layout_probe_total += RanSize<ITEM_COOLTIME>::value;
    ran_layout_probe_total += RanSize<DxEffectRainPoint::LEAVESVERTEX>::value;
    ran_layout_probe_total += RanSize<Line2D>::value;
    ran_layout_probe_total += RanSize<CMList::NODE>::value;
    ran_layout_probe_total += RanSize<DxEffectNeon::OBJECT>::value;
    ran_layout_probe_total += RanSize<DxEffectNeon::OBJECTNORMAL>::value;
    ran_layout_probe_total += RanSize<DxEffectRainPoint::POSITIONBOOL>::value;
    ran_layout_probe_total += RanSize<BLURSYS_PROPERTY::PROPERTY>::value;
    ran_layout_probe_total += RanSize<BLURSYS_PROPERTY::PROPERTY_100>::value;
    ran_layout_probe_total += RanSize<MOVEROTATE_PROPERTY::PROPERTY_101>::value;
    ran_layout_probe_total += RanSize<DxLandGate::PROPERTY_V100>::value;
    ran_layout_probe_total += RanSize<Plane>::value;
    ran_layout_probe_total += RanSize<DxEffectRainPoint::RAINVERTEX>::value;
    ran_layout_probe_total += RanSize<RAIN_PROPERTY>::value;
    ran_layout_probe_total += RanSize<RAIN_PROPERTY_101>::value;
    ran_layout_probe_total += RanSize<RAIN_PROPERTY_103>::value;
    ran_layout_probe_total += RanSize<SACTION_SLOT>::value;
    ran_layout_probe_total += RanSize<SACTIVITY_CHAR_DATA>::value;
    ran_layout_probe_total += RanSize<SANIMCONINFO_104>::value;
    ran_layout_probe_total += RanSize<SANIMSTRIKE>::value;
    ran_layout_probe_total += RanSize<SBONESCALE_100>::value;
    ran_layout_probe_total += RanSize<ITEM::SBOX_ITEM>::value;
    ran_layout_probe_total += RanSize<ITEM::SBOX_ITEM_101>::value;
    ran_layout_probe_total += RanSize<SCHARSKILL>::value;
    ran_layout_probe_total += RanSize<SCODEX_CHAR_DATA>::value;
    ran_layout_probe_total += RanSize<SCROWATTACK_100>::value;
    ran_layout_probe_total += RanSize<SCROWATTACK_101>::value;
    ran_layout_probe_total += RanSize<SCROWATTACK_102>::value;
    ran_layout_probe_total += RanSize<SCROWATTACK_103>::value;
    ran_layout_probe_total += RanSize<SCROWATTACK_104>::value;
    ran_layout_probe_total += RanSize<SCROWBASIC>::value;
    ran_layout_probe_total += RanSize<SCROWPATTERN>::value;
    ran_layout_probe_total += RanSize<SChaSoundData>::value;
    ran_layout_probe_total += RanSize<SChaSoundData_103>::value;
    ran_layout_probe_total += RanSize<ITEM::SELFBUFFS_ITEM>::value;
    ran_layout_probe_total += RanSize<ITEM::SELFBUFFS_ITEM_101>::value;
    ran_layout_probe_total += RanSize<DxEffectGrass::SEPARATEOBJ>::value;
    ran_layout_probe_total += RanSize<SFITEMFACT>::value;
    ran_layout_probe_total += RanSize<SINVENITEM_SAVE>::value;
    ran_layout_probe_total += RanSize<SITEMCUSTOM>::value;
    ran_layout_probe_total += RanSize<SLAND_FILEMARK>::value;
    ran_layout_probe_total += RanSize<SLAND_FILEMARK_100>::value;
    ran_layout_probe_total += RanSize<SLEVEL_HEAD_100>::value;
    ran_layout_probe_total += RanSize<SMOBACTION>::value;
    ran_layout_probe_total += RanSize<SMatrixKey>::value;
    ran_layout_probe_total += RanSize<SNATIVEID>::value;
    ran_layout_probe_total += RanSize<DxEffectRainPoint::SNOWVERTEX>::value;
    ran_layout_probe_total += RanSize<SNOW_PROPERTY>::value;
    ran_layout_probe_total += RanSize<SNOW_PROPERTY_101>::value;
    ran_layout_probe_total += RanSize<SNPC_BUFF>::value;
    ran_layout_probe_total += RanSize<SNPC_ITEM>::value;
    ran_layout_probe_total += RanSize<SNaviIntegrityLog>::value;
    ran_layout_probe_total += RanSize<ITEM::SPETSKINPACKITEMDATA>::value;
    ran_layout_probe_total += RanSize<SParticleData>::value;
    ran_layout_probe_total += RanSize<SPositionKey>::value;
    ran_layout_probe_total += RanSize<SQuatPosKey>::value;
    ran_layout_probe_total += RanSize<ITEM::SRANDOMITEM>::value;
    ran_layout_probe_total += RanSize<ITEM::SRANDOMITEM_100>::value;
    ran_layout_probe_total += RanSize<SRotateKey>::value;
    ran_layout_probe_total += RanSize<SScaleKey>::value;
    ran_layout_probe_total += RanSize<SSkillCondition>::value;
    ran_layout_probe_total += RanSize<CD3DFont::STEXUV>::value;
    ran_layout_probe_total += RanSize<TILE>::value;
    ran_layout_probe_total += RanSize<TILE_TEX>::value;
    ran_layout_probe_total += RanSize<DxCubeMap::VERTEX>::value;
    ran_layout_probe_total += RanSize<DxGlowMan::VERTEXCOLOR>::value;
    ran_layout_probe_total += RanSize<VERTEXCOLORTEX2>::value;
    ran_layout_probe_total += RanSize<VERTEX_XT2>::value;
    ran_layout_probe_total += RanSize<DxEffectWaterLight::WATERVERTEX>::value;
    ran_layout_probe_total += RanSize<WEATHER_PROPERTY>::value;
    ran_layout_probe_total += RanSize<WEATHER_PROPERTY_101>::value;
    ran_layout_probe_total += RanSize<WEATHER_PROPERTY_102>::value;
}
