// D3DX math — the out-of-line half of d3dx9math.h.
//
// The DX SDK header declares these; d3dx9.lib defined them. Everything here is
// pure arithmetic with no GPU involvement, and every convention below matches
// D3DX exactly: matrices are ROW-major with row-vector semantics (v' = v * M),
// so translation lives in the FOURTH ROW (_41.._43), and D3DXMatrixMultiply(a,b)
// means "apply a, then b".
//
// Getting a convention wrong here would not fail to compile — it would tilt the
// whole world silently, so each function mirrors the documented D3DX formula.

#include "windows.h"
#include <d3dx9.h>
#include <math.h>

// --------------------------------------------------------------------- vectors
D3DXVECTOR2 *WINAPI D3DXVec2Normalize(D3DXVECTOR2 *pOut, const D3DXVECTOR2 *pV) {
    float len = sqrtf(pV->x * pV->x + pV->y * pV->y);
    if (len > 0.0f) { pOut->x = pV->x / len; pOut->y = pV->y / len; }
    else            { pOut->x = 0.0f; pOut->y = 0.0f; }
    return pOut;
}

D3DXVECTOR3 *WINAPI D3DXVec3Normalize(D3DXVECTOR3 *pOut, const D3DXVECTOR3 *pV) {
    float len = sqrtf(pV->x * pV->x + pV->y * pV->y + pV->z * pV->z);
    if (len > 0.0f) { pOut->x = pV->x / len; pOut->y = pV->y / len; pOut->z = pV->z / len; }
    else            { pOut->x = pOut->y = pOut->z = 0.0f; }
    return pOut;
}

// v' = (v,1) * M, then divide by w — the "point" transform.
D3DXVECTOR3 *WINAPI D3DXVec3TransformCoord(D3DXVECTOR3 *pOut, const D3DXVECTOR3 *pV,
                                           const D3DXMATRIX *pM) {
    float x = pV->x, y = pV->y, z = pV->z;
    float ox = x * pM->_11 + y * pM->_21 + z * pM->_31 + pM->_41;
    float oy = x * pM->_12 + y * pM->_22 + z * pM->_32 + pM->_42;
    float oz = x * pM->_13 + y * pM->_23 + z * pM->_33 + pM->_43;
    float ow = x * pM->_14 + y * pM->_24 + z * pM->_34 + pM->_44;
    if (ow != 0.0f) { ox /= ow; oy /= ow; oz /= ow; }
    pOut->x = ox; pOut->y = oy; pOut->z = oz;
    return pOut;
}

// v' = (v,0) * M — the "direction" transform: translation is ignored.
D3DXVECTOR3 *WINAPI D3DXVec3TransformNormal(D3DXVECTOR3 *pOut, const D3DXVECTOR3 *pV,
                                            const D3DXMATRIX *pM) {
    float x = pV->x, y = pV->y, z = pV->z;
    float ox = x * pM->_11 + y * pM->_21 + z * pM->_31;
    float oy = x * pM->_12 + y * pM->_22 + z * pM->_32;
    float oz = x * pM->_13 + y * pM->_23 + z * pM->_33;
    pOut->x = ox; pOut->y = oy; pOut->z = oz;
    return pOut;
}

D3DXVECTOR4 *WINAPI D3DXVec4Transform(D3DXVECTOR4 *pOut, const D3DXVECTOR4 *pV,
                                      const D3DXMATRIX *pM) {
    float x = pV->x, y = pV->y, z = pV->z, w = pV->w;
    pOut->x = x * pM->_11 + y * pM->_21 + z * pM->_31 + w * pM->_41;
    pOut->y = x * pM->_12 + y * pM->_22 + z * pM->_32 + w * pM->_42;
    pOut->z = x * pM->_13 + y * pM->_23 + z * pM->_33 + w * pM->_43;
    pOut->w = x * pM->_14 + y * pM->_24 + z * pM->_34 + w * pM->_44;
    return pOut;
}

// World -> screen: transform by world*view*proj, then map NDC into the viewport.
D3DXVECTOR3 *WINAPI D3DXVec3Project(D3DXVECTOR3 *pOut, const D3DXVECTOR3 *pV,
                                    const D3DVIEWPORT9 *pViewport,
                                    const D3DXMATRIX *pProjection,
                                    const D3DXMATRIX *pView,
                                    const D3DXMATRIX *pWorld) {
    D3DXMATRIX m; D3DXMatrixIdentity(&m);
    if (pWorld)      D3DXMatrixMultiply(&m, &m, pWorld);
    if (pView)       D3DXMatrixMultiply(&m, &m, pView);
    if (pProjection) D3DXMatrixMultiply(&m, &m, pProjection);
    D3DXVECTOR3 v;
    D3DXVec3TransformCoord(&v, pV, &m);
    if (pViewport) {
        pOut->x = pViewport->X + (1.0f + v.x) * pViewport->Width  * 0.5f;
        pOut->y = pViewport->Y + (1.0f - v.y) * pViewport->Height * 0.5f;
        pOut->z = pViewport->MinZ + v.z * (pViewport->MaxZ - pViewport->MinZ);
    } else {
        *pOut = v;
    }
    return pOut;
}

D3DXVECTOR3 *WINAPI D3DXVec3CatmullRom(D3DXVECTOR3 *pOut, const D3DXVECTOR3 *pV0,
                                       const D3DXVECTOR3 *pV1, const D3DXVECTOR3 *pV2,
                                       const D3DXVECTOR3 *pV3, FLOAT s) {
    float s2 = s * s, s3 = s2 * s;
    float f0 = -s3 + 2.0f * s2 - s;
    float f1 = 3.0f * s3 - 5.0f * s2 + 2.0f;
    float f2 = -3.0f * s3 + 4.0f * s2 + s;
    float f3 = s3 - s2;
    pOut->x = 0.5f * (pV0->x * f0 + pV1->x * f1 + pV2->x * f2 + pV3->x * f3);
    pOut->y = 0.5f * (pV0->y * f0 + pV1->y * f1 + pV2->y * f2 + pV3->y * f3);
    pOut->z = 0.5f * (pV0->z * f0 + pV1->z * f1 + pV2->z * f2 + pV3->z * f3);
    return pOut;
}

// -------------------------------------------------------------------- matrices
D3DXMATRIX *WINAPI D3DXMatrixMultiply(D3DXMATRIX *pOut, const D3DXMATRIX *pM1,
                                      const D3DXMATRIX *pM2) {
    D3DXMATRIX r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r.m[i][j] = pM1->m[i][0] * pM2->m[0][j] + pM1->m[i][1] * pM2->m[1][j]
                      + pM1->m[i][2] * pM2->m[2][j] + pM1->m[i][3] * pM2->m[3][j];
    *pOut = r;
    return pOut;
}

D3DXMATRIX *WINAPI D3DXMatrixTranspose(D3DXMATRIX *pOut, const D3DXMATRIX *pM) {
    D3DXMATRIX r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) r.m[i][j] = pM->m[j][i];
    *pOut = r;
    return pOut;
}

// Cofactor expansion; returns NULL when singular, exactly as D3DX does.
D3DXMATRIX *WINAPI D3DXMatrixInverse(D3DXMATRIX *pOut, FLOAT *pDeterminant,
                                     const D3DXMATRIX *pM) {
    const float *m = &pM->_11;
    float inv[16];

    inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15]
             + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15]
             - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8]  =  m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15]
             + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14]
             - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15]
             - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15]
             + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9]  = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15]
             - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] =  m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14]
             + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2]  =  m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15]
             + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    inv[6]  = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15]
             - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    inv[10] =  m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15]
             + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14]
             - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
    inv[3]  = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11]
             - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    inv[7]  =  m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11]
             + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11]
             - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    inv[15] =  m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10]
             + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];

    float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (pDeterminant) *pDeterminant = det;
    if (det == 0.0f) return NULL;

    float d = 1.0f / det;
    float *o = &pOut->_11;
    for (int i = 0; i < 16; ++i) o[i] = inv[i] * d;
    return pOut;
}

D3DXMATRIX *WINAPI D3DXMatrixScaling(D3DXMATRIX *pOut, FLOAT sx, FLOAT sy, FLOAT sz) {
    D3DXMatrixIdentity(pOut);
    pOut->_11 = sx; pOut->_22 = sy; pOut->_33 = sz;
    return pOut;
}

D3DXMATRIX *WINAPI D3DXMatrixTranslation(D3DXMATRIX *pOut, FLOAT x, FLOAT y, FLOAT z) {
    D3DXMatrixIdentity(pOut);
    pOut->_41 = x; pOut->_42 = y; pOut->_43 = z;   // row-vector: translation in row 4
    return pOut;
}

D3DXMATRIX *WINAPI D3DXMatrixRotationX(D3DXMATRIX *pOut, FLOAT Angle) {
    float c = cosf(Angle), s = sinf(Angle);
    D3DXMatrixIdentity(pOut);
    pOut->_22 = c; pOut->_23 = s;
    pOut->_32 = -s; pOut->_33 = c;
    return pOut;
}

D3DXMATRIX *WINAPI D3DXMatrixRotationY(D3DXMATRIX *pOut, FLOAT Angle) {
    float c = cosf(Angle), s = sinf(Angle);
    D3DXMatrixIdentity(pOut);
    pOut->_11 = c; pOut->_13 = -s;
    pOut->_31 = s; pOut->_33 = c;
    return pOut;
}

D3DXMATRIX *WINAPI D3DXMatrixRotationZ(D3DXMATRIX *pOut, FLOAT Angle) {
    float c = cosf(Angle), s = sinf(Angle);
    D3DXMatrixIdentity(pOut);
    pOut->_11 = c; pOut->_12 = s;
    pOut->_21 = -s; pOut->_22 = c;
    return pOut;
}

D3DXMATRIX *WINAPI D3DXMatrixRotationAxis(D3DXMATRIX *pOut, const D3DXVECTOR3 *pV, FLOAT Angle) {
    D3DXVECTOR3 v;
    D3DXVec3Normalize(&v, pV);
    float c = cosf(Angle), s = sinf(Angle), t = 1.0f - c;
    D3DXMatrixIdentity(pOut);
    pOut->_11 = t * v.x * v.x + c;
    pOut->_12 = t * v.x * v.y + s * v.z;
    pOut->_13 = t * v.x * v.z - s * v.y;
    pOut->_21 = t * v.x * v.y - s * v.z;
    pOut->_22 = t * v.y * v.y + c;
    pOut->_23 = t * v.y * v.z + s * v.x;
    pOut->_31 = t * v.x * v.z + s * v.y;
    pOut->_32 = t * v.y * v.z - s * v.x;
    pOut->_33 = t * v.z * v.z + c;
    return pOut;
}

D3DXMATRIX *WINAPI D3DXMatrixRotationQuaternion(D3DXMATRIX *pOut, const D3DXQUATERNION *pQ) {
    float x = pQ->x, y = pQ->y, z = pQ->z, w = pQ->w;
    D3DXMatrixIdentity(pOut);
    pOut->_11 = 1.0f - 2.0f * (y * y + z * z);
    pOut->_12 = 2.0f * (x * y + z * w);
    pOut->_13 = 2.0f * (x * z - y * w);
    pOut->_21 = 2.0f * (x * y - z * w);
    pOut->_22 = 1.0f - 2.0f * (x * x + z * z);
    pOut->_23 = 2.0f * (y * z + x * w);
    pOut->_31 = 2.0f * (x * z + y * w);
    pOut->_32 = 2.0f * (y * z - x * w);
    pOut->_33 = 1.0f - 2.0f * (x * x + y * y);
    return pOut;
}

D3DXMATRIX *WINAPI D3DXMatrixRotationYawPitchRoll(D3DXMATRIX *pOut, FLOAT Yaw, FLOAT Pitch, FLOAT Roll) {
    D3DXQUATERNION q;
    D3DXQuaternionRotationYawPitchRoll(&q, Yaw, Pitch, Roll);
    return D3DXMatrixRotationQuaternion(pOut, &q);
}

D3DXMATRIX *WINAPI D3DXMatrixLookAtLH(D3DXMATRIX *pOut, const D3DXVECTOR3 *pEye,
                                      const D3DXVECTOR3 *pAt, const D3DXVECTOR3 *pUp) {
    D3DXVECTOR3 zaxis(pAt->x - pEye->x, pAt->y - pEye->y, pAt->z - pEye->z);
    D3DXVec3Normalize(&zaxis, &zaxis);
    D3DXVECTOR3 xaxis;
    D3DXVec3Cross(&xaxis, pUp, &zaxis);
    D3DXVec3Normalize(&xaxis, &xaxis);
    D3DXVECTOR3 yaxis;
    D3DXVec3Cross(&yaxis, &zaxis, &xaxis);

    D3DXMatrixIdentity(pOut);
    pOut->_11 = xaxis.x; pOut->_12 = yaxis.x; pOut->_13 = zaxis.x;
    pOut->_21 = xaxis.y; pOut->_22 = yaxis.y; pOut->_23 = zaxis.y;
    pOut->_31 = xaxis.z; pOut->_32 = yaxis.z; pOut->_33 = zaxis.z;
    pOut->_41 = -D3DXVec3Dot(&xaxis, pEye);
    pOut->_42 = -D3DXVec3Dot(&yaxis, pEye);
    pOut->_43 = -D3DXVec3Dot(&zaxis, pEye);
    return pOut;
}

D3DXMATRIX *WINAPI D3DXMatrixPerspectiveFovLH(D3DXMATRIX *pOut, FLOAT fovy, FLOAT Aspect,
                                              FLOAT zn, FLOAT zf) {
    float yScale = 1.0f / tanf(fovy * 0.5f);
    float xScale = yScale / Aspect;
    memset(pOut, 0, sizeof(*pOut));
    pOut->_11 = xScale;
    pOut->_22 = yScale;
    pOut->_33 = zf / (zf - zn);
    pOut->_34 = 1.0f;
    pOut->_43 = -zn * zf / (zf - zn);
    return pOut;
}

D3DXMATRIX *WINAPI D3DXMatrixOrthoLH(D3DXMATRIX *pOut, FLOAT w, FLOAT h, FLOAT zn, FLOAT zf) {
    D3DXMatrixIdentity(pOut);
    pOut->_11 = 2.0f / w;
    pOut->_22 = 2.0f / h;
    pOut->_33 = 1.0f / (zf - zn);
    pOut->_43 = zn / (zn - zf);
    return pOut;
}

// ----------------------------------------------------------------- quaternions
D3DXQUATERNION *WINAPI D3DXQuaternionNormalize(D3DXQUATERNION *pOut, const D3DXQUATERNION *pQ) {
    float len = sqrtf(pQ->x * pQ->x + pQ->y * pQ->y + pQ->z * pQ->z + pQ->w * pQ->w);
    if (len > 0.0f) { pOut->x = pQ->x / len; pOut->y = pQ->y / len; pOut->z = pQ->z / len; pOut->w = pQ->w / len; }
    else            { pOut->x = pOut->y = pOut->z = 0.0f; pOut->w = 1.0f; }
    return pOut;
}

D3DXQUATERNION *WINAPI D3DXQuaternionInverse(D3DXQUATERNION *pOut, const D3DXQUATERNION *pQ) {
    float n = pQ->x * pQ->x + pQ->y * pQ->y + pQ->z * pQ->z + pQ->w * pQ->w;
    if (n == 0.0f) { pOut->x = pOut->y = pOut->z = 0.0f; pOut->w = 1.0f; return pOut; }
    float d = 1.0f / n;
    pOut->x = -pQ->x * d; pOut->y = -pQ->y * d; pOut->z = -pQ->z * d; pOut->w = pQ->w * d;
    return pOut;
}

// D3DX order: out = q2 * q1 (q1 applied first) — matches the SDK, not the
// textbook Hamilton order.
D3DXQUATERNION *WINAPI D3DXQuaternionMultiply(D3DXQUATERNION *pOut, const D3DXQUATERNION *pQ1,
                                              const D3DXQUATERNION *pQ2) {
    D3DXQUATERNION r;
    r.x = pQ2->w * pQ1->x + pQ2->x * pQ1->w + pQ2->y * pQ1->z - pQ2->z * pQ1->y;
    r.y = pQ2->w * pQ1->y - pQ2->x * pQ1->z + pQ2->y * pQ1->w + pQ2->z * pQ1->x;
    r.z = pQ2->w * pQ1->z + pQ2->x * pQ1->y - pQ2->y * pQ1->x + pQ2->z * pQ1->w;
    r.w = pQ2->w * pQ1->w - pQ2->x * pQ1->x - pQ2->y * pQ1->y - pQ2->z * pQ1->z;
    *pOut = r;
    return pOut;
}

D3DXQUATERNION *WINAPI D3DXQuaternionRotationMatrix(D3DXQUATERNION *pOut, const D3DXMATRIX *pM) {
    float tr = pM->_11 + pM->_22 + pM->_33;
    if (tr > 0.0f) {
        float s = sqrtf(tr + 1.0f) * 2.0f;
        pOut->w = 0.25f * s;
        pOut->x = (pM->_23 - pM->_32) / s;
        pOut->y = (pM->_31 - pM->_13) / s;
        pOut->z = (pM->_12 - pM->_21) / s;
    } else if (pM->_11 > pM->_22 && pM->_11 > pM->_33) {
        float s = sqrtf(1.0f + pM->_11 - pM->_22 - pM->_33) * 2.0f;
        pOut->w = (pM->_23 - pM->_32) / s;
        pOut->x = 0.25f * s;
        pOut->y = (pM->_12 + pM->_21) / s;
        pOut->z = (pM->_31 + pM->_13) / s;
    } else if (pM->_22 > pM->_33) {
        float s = sqrtf(1.0f + pM->_22 - pM->_11 - pM->_33) * 2.0f;
        pOut->w = (pM->_31 - pM->_13) / s;
        pOut->x = (pM->_12 + pM->_21) / s;
        pOut->y = 0.25f * s;
        pOut->z = (pM->_23 + pM->_32) / s;
    } else {
        float s = sqrtf(1.0f + pM->_33 - pM->_11 - pM->_22) * 2.0f;
        pOut->w = (pM->_12 - pM->_21) / s;
        pOut->x = (pM->_31 + pM->_13) / s;
        pOut->y = (pM->_23 + pM->_32) / s;
        pOut->z = 0.25f * s;
    }
    return pOut;
}

D3DXQUATERNION *WINAPI D3DXQuaternionRotationAxis(D3DXQUATERNION *pOut, const D3DXVECTOR3 *pV,
                                                  FLOAT Angle) {
    D3DXVECTOR3 v;
    D3DXVec3Normalize(&v, pV);
    float h = Angle * 0.5f, s = sinf(h);
    pOut->x = v.x * s; pOut->y = v.y * s; pOut->z = v.z * s; pOut->w = cosf(h);
    return pOut;
}

D3DXQUATERNION *WINAPI D3DXQuaternionRotationYawPitchRoll(D3DXQUATERNION *pOut, FLOAT Yaw,
                                                          FLOAT Pitch, FLOAT Roll) {
    float sy = sinf(Yaw * 0.5f),   cy = cosf(Yaw * 0.5f);
    float sp = sinf(Pitch * 0.5f), cp = cosf(Pitch * 0.5f);
    float sr = sinf(Roll * 0.5f),  cr = cosf(Roll * 0.5f);
    pOut->x = cy * sp * cr + sy * cp * sr;
    pOut->y = sy * cp * cr - cy * sp * sr;
    pOut->z = cy * cp * sr - sy * sp * cr;
    pOut->w = cy * cp * cr + sy * sp * sr;
    return pOut;
}

D3DXQUATERNION *WINAPI D3DXQuaternionSlerp(D3DXQUATERNION *pOut, const D3DXQUATERNION *pQ1,
                                           const D3DXQUATERNION *pQ2, FLOAT t) {
    float dot = pQ1->x * pQ2->x + pQ1->y * pQ2->y + pQ1->z * pQ2->z + pQ1->w * pQ2->w;
    float sign = 1.0f;
    if (dot < 0.0f) { dot = -dot; sign = -1.0f; }   // take the short way round
    float k0, k1;
    if (dot > 0.9995f) {                            // nearly parallel: lerp
        k0 = 1.0f - t;
        k1 = t * sign;
    } else {
        float theta = acosf(dot), st = sinf(theta);
        k0 = sinf((1.0f - t) * theta) / st;
        k1 = sinf(t * theta) / st * sign;
    }
    pOut->x = k0 * pQ1->x + k1 * pQ2->x;
    pOut->y = k0 * pQ1->y + k1 * pQ2->y;
    pOut->z = k0 * pQ1->z + k1 * pQ2->z;
    pOut->w = k0 * pQ1->w + k1 * pQ2->w;
    return pOut;
}

D3DXQUATERNION *WINAPI D3DXQuaternionSquad(D3DXQUATERNION *pOut, const D3DXQUATERNION *pQ1,
                                           const D3DXQUATERNION *pA, const D3DXQUATERNION *pB,
                                           const D3DXQUATERNION *pC, FLOAT t) {
    D3DXQUATERNION q1, q2;
    D3DXQuaternionSlerp(&q1, pQ1, pC, t);
    D3DXQuaternionSlerp(&q2, pA, pB, t);
    return D3DXQuaternionSlerp(pOut, &q1, &q2, 2.0f * t * (1.0f - t));
}

void WINAPI D3DXQuaternionSquadSetup(D3DXQUATERNION *pAOut, D3DXQUATERNION *pBOut,
                                     D3DXQUATERNION *pCOut, const D3DXQUATERNION *pQ0,
                                     const D3DXQUATERNION *pQ1, const D3DXQUATERNION *pQ2,
                                     const D3DXQUATERNION *pQ3) {
    // Flip each control point onto the same hemisphere as its neighbour, then
    // build the inner tangents (the standard D3DX construction).
    D3DXQUATERNION q0 = *pQ0, q2 = *pQ2, q3 = *pQ3;
    if (D3DXQuaternionDot(pQ0, pQ1) < 0.0f) { q0.x = -q0.x; q0.y = -q0.y; q0.z = -q0.z; q0.w = -q0.w; }
    if (D3DXQuaternionDot(pQ1, pQ2) < 0.0f) { q2.x = -q2.x; q2.y = -q2.y; q2.z = -q2.z; q2.w = -q2.w; }
    if (D3DXQuaternionDot(&q2, pQ3) < 0.0f) { q3.x = -q3.x; q3.y = -q3.y; q3.z = -q3.z; q3.w = -q3.w; }

    D3DXQUATERNION invQ1, invQ2, lnA, lnB, tmp;
    D3DXQuaternionInverse(&invQ1, pQ1);
    D3DXQuaternionMultiply(&tmp, &invQ1, &q2);  D3DXQuaternionLn(&lnA, &tmp);
    D3DXQuaternionMultiply(&tmp, &invQ1, &q0);  D3DXQuaternionLn(&lnB, &tmp);
    D3DXQUATERNION e;
    e.x = -0.25f * (lnA.x + lnB.x); e.y = -0.25f * (lnA.y + lnB.y);
    e.z = -0.25f * (lnA.z + lnB.z); e.w = -0.25f * (lnA.w + lnB.w);
    D3DXQuaternionExp(&tmp, &e);
    D3DXQuaternionMultiply(pAOut, pQ1, &tmp);

    D3DXQuaternionInverse(&invQ2, &q2);
    D3DXQuaternionMultiply(&tmp, &invQ2, &q3); D3DXQuaternionLn(&lnA, &tmp);
    D3DXQuaternionMultiply(&tmp, &invQ2, pQ1); D3DXQuaternionLn(&lnB, &tmp);
    e.x = -0.25f * (lnA.x + lnB.x); e.y = -0.25f * (lnA.y + lnB.y);
    e.z = -0.25f * (lnA.z + lnB.z); e.w = -0.25f * (lnA.w + lnB.w);
    D3DXQuaternionExp(&tmp, &e);
    D3DXQuaternionMultiply(pBOut, &q2, &tmp);

    *pCOut = q2;
}

// ---------------------------------------------------------------------- planes
D3DXPLANE *WINAPI D3DXPlaneFromPointNormal(D3DXPLANE *pOut, const D3DXVECTOR3 *pPoint,
                                           const D3DXVECTOR3 *pNormal) {
    pOut->a = pNormal->x; pOut->b = pNormal->y; pOut->c = pNormal->z;
    pOut->d = -D3DXVec3Dot(pPoint, pNormal);
    return pOut;
}

D3DXPLANE *WINAPI D3DXPlaneFromPoints(D3DXPLANE *pOut, const D3DXVECTOR3 *pV1,
                                      const D3DXVECTOR3 *pV2, const D3DXVECTOR3 *pV3) {
    D3DXVECTOR3 e1(pV2->x - pV1->x, pV2->y - pV1->y, pV2->z - pV1->z);
    D3DXVECTOR3 e2(pV3->x - pV1->x, pV3->y - pV1->y, pV3->z - pV1->z);
    D3DXVECTOR3 n;
    D3DXVec3Cross(&n, &e1, &e2);
    D3DXVec3Normalize(&n, &n);
    return D3DXPlaneFromPointNormal(pOut, pV1, &n);
}

D3DXVECTOR3 *WINAPI D3DXPlaneIntersectLine(D3DXVECTOR3 *pOut, const D3DXPLANE *pP,
                                           const D3DXVECTOR3 *pV1, const D3DXVECTOR3 *pV2) {
    D3DXVECTOR3 dir(pV2->x - pV1->x, pV2->y - pV1->y, pV2->z - pV1->z);
    D3DXVECTOR3 n(pP->a, pP->b, pP->c);
    float denom = D3DXVec3Dot(&n, &dir);
    if (denom == 0.0f) return NULL;                 // parallel: D3DX returns NULL
    float t = -(D3DXVec3Dot(&n, pV1) + pP->d) / denom;
    pOut->x = pV1->x + t * dir.x;
    pOut->y = pV1->y + t * dir.y;
    pOut->z = pV1->z + t * dir.z;
    return pOut;
}

// A plane transforms by the inverse-transpose of the matrix.
D3DXPLANE *WINAPI D3DXPlaneTransform(D3DXPLANE *pOut, const D3DXPLANE *pP, const D3DXMATRIX *pM) {
    float x = pP->a, y = pP->b, z = pP->c, d = pP->d;
    pOut->a = x * pM->_11 + y * pM->_21 + z * pM->_31 + d * pM->_41;
    pOut->b = x * pM->_12 + y * pM->_22 + z * pM->_32 + d * pM->_42;
    pOut->c = x * pM->_13 + y * pM->_23 + z * pM->_33 + d * pM->_43;
    pOut->d = x * pM->_14 + y * pM->_24 + z * pM->_34 + d * pM->_44;
    return pOut;
}

// ------------------------------------------------------------------- FVF / misc
UINT WINAPI D3DXGetFVFVertexSize(DWORD FVF) {
    UINT size = 0;
    switch (FVF & D3DFVF_POSITION_MASK) {
        case D3DFVF_XYZ:    size += 3 * sizeof(float); break;
        case D3DFVF_XYZRHW: size += 4 * sizeof(float); break;
        case D3DFVF_XYZB1:  size += 4 * sizeof(float); break;
        case D3DFVF_XYZB2:  size += 5 * sizeof(float); break;
        case D3DFVF_XYZB3:  size += 6 * sizeof(float); break;
        case D3DFVF_XYZB4:  size += 7 * sizeof(float); break;
        case D3DFVF_XYZB5:  size += 8 * sizeof(float); break;
        case D3DFVF_XYZW:   size += 4 * sizeof(float); break;
        default: break;
    }
    if (FVF & D3DFVF_NORMAL)   size += 3 * sizeof(float);
    if (FVF & D3DFVF_PSIZE)    size += sizeof(float);
    if (FVF & D3DFVF_DIFFUSE)  size += sizeof(DWORD);
    if (FVF & D3DFVF_SPECULAR) size += sizeof(DWORD);

    UINT numTex = (FVF & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    for (UINT i = 0; i < numTex; ++i) {
        // Two bits per texture coord set say how many floats it has.
        DWORD fmt = (FVF >> (16 + i * 2)) & 0x3;
        switch (fmt) {
            case D3DFVF_TEXTUREFORMAT1: size += 1 * sizeof(float); break;
            case D3DFVF_TEXTUREFORMAT2: size += 2 * sizeof(float); break;
            case D3DFVF_TEXTUREFORMAT3: size += 3 * sizeof(float); break;
            case D3DFVF_TEXTUREFORMAT4: size += 4 * sizeof(float); break;
            default:                    size += 2 * sizeof(float); break;
        }
    }
    return size;
}

// Ritter's bounding sphere: centroid, then grow to cover the farthest point.
HRESULT WINAPI D3DXComputeBoundingSphere(const D3DXVECTOR3 *pFirstPosition, DWORD NumVertices,
                                         DWORD dwStride, D3DXVECTOR3 *pCenter, FLOAT *pRadius) {
    if (!pFirstPosition || !pCenter || !pRadius || NumVertices == 0) return D3DERR_INVALIDCALL;
    const BYTE *p = (const BYTE *)pFirstPosition;
    D3DXVECTOR3 c(0, 0, 0);
    for (DWORD i = 0; i < NumVertices; ++i) {
        const D3DXVECTOR3 *v = (const D3DXVECTOR3 *)(p + i * dwStride);
        c.x += v->x; c.y += v->y; c.z += v->z;
    }
    c.x /= NumVertices; c.y /= NumVertices; c.z /= NumVertices;
    float r2 = 0.0f;
    for (DWORD i = 0; i < NumVertices; ++i) {
        const D3DXVECTOR3 *v = (const D3DXVECTOR3 *)(p + i * dwStride);
        float dx = v->x - c.x, dy = v->y - c.y, dz = v->z - c.z;
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > r2) r2 = d2;
    }
    *pCenter = c;
    *pRadius = sqrtf(r2);
    return S_OK;
}

// FVF -> vertex declaration. The mesh returns this from GetDeclaration; the
// engine only ever reads it back to size a vertex, so the order follows the
// FVF's own fixed layout.

HRESULT WINAPI D3DXDeclaratorFromFVF(DWORD FVF, D3DVERTEXELEMENT9 decl[MAX_FVF_DECL_SIZE]) {
    if (!decl) return D3DERR_INVALIDCALL;
    int n = 0;
    WORD offset = 0;
    #define PUSH(type, usage, index)                                        \
        do {                                                                \
            decl[n].Stream = 0; decl[n].Offset = offset;                    \
            decl[n].Type = (BYTE)(type); decl[n].Method = D3DDECLMETHOD_DEFAULT; \
            decl[n].Usage = (BYTE)(usage); decl[n].UsageIndex = (BYTE)(index); \
            ++n;                                                            \
        } while (0)

    switch (FVF & D3DFVF_POSITION_MASK) {
        case D3DFVF_XYZRHW: PUSH(D3DDECLTYPE_FLOAT4, D3DDECLUSAGE_POSITIONT, 0); offset += 16; break;
        case D3DFVF_XYZ:    PUSH(D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_POSITION, 0);  offset += 12; break;
        default:            PUSH(D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_POSITION, 0);  offset += 12; break;
    }
    if (FVF & D3DFVF_NORMAL)   { PUSH(D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_NORMAL, 0);   offset += 12; }
    if (FVF & D3DFVF_PSIZE)    { PUSH(D3DDECLTYPE_FLOAT1, D3DDECLUSAGE_PSIZE, 0);    offset += 4; }
    if (FVF & D3DFVF_DIFFUSE)  { PUSH(D3DDECLTYPE_D3DCOLOR, D3DDECLUSAGE_COLOR, 0);  offset += 4; }
    if (FVF & D3DFVF_SPECULAR) { PUSH(D3DDECLTYPE_D3DCOLOR, D3DDECLUSAGE_COLOR, 1);  offset += 4; }

    UINT numTex = (FVF & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    for (UINT i = 0; i < numTex && n < MAX_FVF_DECL_SIZE - 1; ++i) {
        DWORD fmt = (FVF >> (16 + i * 2)) & 0x3;
        BYTE type = D3DDECLTYPE_FLOAT2;
        WORD size = 8;
        if (fmt == D3DFVF_TEXTUREFORMAT1) { type = D3DDECLTYPE_FLOAT1; size = 4; }
        else if (fmt == D3DFVF_TEXTUREFORMAT3) { type = D3DDECLTYPE_FLOAT3; size = 12; }
        else if (fmt == D3DFVF_TEXTUREFORMAT4) { type = D3DDECLTYPE_FLOAT4; size = 16; }
        PUSH(type, D3DDECLUSAGE_TEXCOORD, i);
        offset += size;
    }
    #undef PUSH

    D3DVERTEXELEMENT9 end = D3DDECL_END();
    decl[n] = end;
    return D3D_OK;
}
