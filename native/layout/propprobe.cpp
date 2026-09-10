#include "stdafx.h"
#include "DxEffectMesh.h"
#include "DxEffectParticleSys.h"
#include "DxEffectBlurSys.h"
#include "DxEffectCamera.h"
#include "DxEffectDecal.h"
#include "DxEffectGround.h"
#include "DxEffectLighting.h"
#include "DxEffectMoveRotate.h"
#include "DxEffectMoveTarget.h"
#include "DxEffectPointLight.h"
#include "DxEffectSequence.h"
#include "DxEffectSkinMesh.h"
#include "DxEffectWave.h"
#include "DxLandGateMan.h"

template <class T> struct RanSize { static const size_t value = sizeof(T); };
size_t ran_layout_probe_total = 0;
void ran_layout_probe() {
    ran_layout_probe_total += RanSize<BLURSYS_PROPERTY::PROPERTY>::value;
    ran_layout_probe_total += RanSize<BLURSYS_PROPERTY::PROPERTY_100>::value;
    ran_layout_probe_total += RanSize<CAMERA_PROPERTY::PROPERTY>::value;
    ran_layout_probe_total += RanSize<DECAL_PROPERTY::PROPERTY>::value;
    ran_layout_probe_total += RanSize<DxLandGate::PROPERTY>::value;
    ran_layout_probe_total += RanSize<DxLandGate::PROPERTY_V100>::value;
    ran_layout_probe_total += RanSize<GROUND_PROPERTY::PROPERTY>::value;
    ran_layout_probe_total += RanSize<GROUND_PROPERTY::PROPERTY_100>::value;
    ran_layout_probe_total += RanSize<LIGHTNING_PROPERTY::PROPERTY>::value;
    ran_layout_probe_total += RanSize<LIGHTNING_PROPERTY::PROPERTY_100>::value;
    ran_layout_probe_total += RanSize<MESH_PROPERTY::PROPERTY>::value;
    ran_layout_probe_total += RanSize<MESH_PROPERTY::PROPERTY_100>::value;
    ran_layout_probe_total += RanSize<MESH_PROPERTY::PROPERTY_101>::value;
    ran_layout_probe_total += RanSize<MESH_PROPERTY::PROPERTY_102>::value;
    ran_layout_probe_total += RanSize<MESH_PROPERTY::PROPERTY_103>::value;
    ran_layout_probe_total += RanSize<MOVEROTATE_PROPERTY::PROPERTY>::value;
    ran_layout_probe_total += RanSize<MOVEROTATE_PROPERTY::PROPERTY_100>::value;
    ran_layout_probe_total += RanSize<MOVEROTATE_PROPERTY::PROPERTY_101>::value;
    ran_layout_probe_total += RanSize<MOVETARGET_PROPERTY::PROPERTY>::value;
    ran_layout_probe_total += RanSize<PARTICLESYS_PROPERTY::PROPERTY>::value;
    ran_layout_probe_total += RanSize<PARTICLESYS_PROPERTY::PROPERTY_100>::value;
    ran_layout_probe_total += RanSize<PARTICLESYS_PROPERTY::PROPERTY_101>::value;
    ran_layout_probe_total += RanSize<PARTICLESYS_PROPERTY::PROPERTY_102_103>::value;
    ran_layout_probe_total += RanSize<PARTICLESYS_PROPERTY::PROPERTY_104>::value;
    ran_layout_probe_total += RanSize<PARTICLESYS_PROPERTY::PROPERTY_105>::value;
    ran_layout_probe_total += RanSize<PARTICLESYS_PROPERTY::PROPERTY_106>::value;
    ran_layout_probe_total += RanSize<POINTLIGHT_PROPERTY::PROPERTY>::value;
    ran_layout_probe_total += RanSize<SEQUENCE_PROPERTY::PROPERTY>::value;
    ran_layout_probe_total += RanSize<SEQUENCE_PROPERTY::PROPERTY_100>::value;
    ran_layout_probe_total += RanSize<SKINMESH_PROPERTY::PROPERTY>::value;
    ran_layout_probe_total += RanSize<WAVE_PROPERTY::PROPERTY>::value;
    ran_layout_probe_total += RanSize<WAVE_PROPERTY::PROPERTY_100>::value;
}
