/**
* @copyright 2024 - Max Bebök
* @license MIT
*/

#include <algorithm>
#include <cstdio>
#include <cassert>
#include <random>
#include <stdexcept>
#include <unordered_map>
#include "converter.h"


uint64_t hashVertex(const V64M::VertexOut &vT3D, uint32_t boneIndex)
{
  uint64_t h = 0xcbf29ce484222325ULL;
  auto mix = [&](uint64_t v) {
    h = (h ^ v) * 0x100000001b3ULL;
  };

  mix((uint64_t)vT3D.pos[0]);
  mix((uint64_t)vT3D.pos[1] << 4);
  mix((uint64_t)vT3D.pos[2] << 6);
  mix((uint64_t)vT3D.norm);
  mix((uint64_t)vT3D.rgba << 12);
  mix((uint64_t)vT3D.s << 14);
  mix((uint64_t)vT3D.t << 16);
  mix(boneIndex);
  return h;
}

void convertVertex(
  float modelScale, float texSizeX, float texSizeY, const V64M::VertexNorm &v, V64M::VertexOut &vT3D,
  const Mat4 &mat, const std::vector<Mat4> &matrices, bool uvAdjust
) {
  //auto posInt = mat * v.pos * modelScale;
  auto posInt = v.pos;

  Mat4 normMat = mat;
  Vec3 norm = v.norm;
  if(v.boneIndex >= 0) {
    // pre-transform position into bone space
    auto boneMat = matrices[v.boneIndex];
    posInt = boneMat * (posInt);

    normMat = boneMat * normMat;
  }

  normMat[3] = Vec4{0.0f, 0.0f, 0.0f, 1.0f};
  norm = (normMat * norm).normalize();

  posInt = mat * posInt * modelScale;
  posInt = (posInt).round();
  vT3D.pos[0] = (int16_t)posInt.x();
  vT3D.pos[1] = (int16_t)posInt.y();
  vT3D.pos[2] = (int16_t)posInt.z();

  auto normPacked = (norm * Vec3{15.5f, 31.5f, 15.5f})
    .round()
    .clamp(
      Vec3{-16.0f, -32.0f, -16.0f},
      Vec3{ 15.0f,  31.0f,  15.0f}
    );

  vT3D.norm = ((int16_t)(normPacked[0]) & 0b11111 ) << 11
            | ((int16_t)(normPacked[1]) & 0b111111) <<  5
            | ((int16_t)(normPacked[2]) & 0b11111 ) <<  0;

  auto safeColor = Vec4{v.color[0], v.color[1], v.color[2], v.color[3]}.clamp(0.0f, 1.0f) * 255.0f;
  vT3D.rgba = (uint32_t)(safeColor[3]);
  vT3D.rgba |= (uint32_t)(safeColor[2]) << 8;
  vT3D.rgba |= (uint32_t)(safeColor[1]) << 16;
  vT3D.rgba |= (uint32_t)(safeColor[0]) << 24;

  // Enable this to debug bone-indices:
  /*if(v.boneIndex >= 0) {
    vT3D.rgba = 0xFF;
    vT3D.rgba |= (uint32_t)((uint32_t)((v.boneIndex+1) * 180) % 256) << 8;
    vT3D.rgba |= (uint32_t)((uint32_t)((v.boneIndex+1) * 80) % 256) << 16;
    vT3D.rgba |= (uint32_t)((uint32_t)((v.boneIndex+1) * 50) % 256) << 24;
  } else {
    vT3D.rgba = 0xFFFFFFFF;
  }*/

  /* RDP 10.5 texel coords baked at export, exactly as tiny3d did: the s16
   * wraps stay periodic with the texture, which extreme UVs (single-color
   * palette points) rely on. The texturing uniform stays at identity. */
  vT3D.s = (int16_t)(int32_t)(v.uv[0] * texSizeX * 32.0f);
  vT3D.t = (int16_t)(int32_t)(v.uv[1] * texSizeY * 32.0f);

  if(uvAdjust) {
    vT3D.s -= 16.0f;
    vT3D.t -= 16.0f;
  }

  vT3D.hash = hashVertex(vT3D, v.boneIndex);
  vT3D.boneIndex = v.boneIndex;
}

/* Flattens one object for the magma ucode: deduplicated vertices plus a
 * plain u16 triangle index list. The triangle order is already vertex-cache
 * friendly (meshopt ran in the parser); the draw batches against the cache
 * at runtime, so nothing about the cache is baked in. */
V64M::ModelFlat flattenModel(const V64M::Model &model)
{
  V64M::ModelFlat res{
    .aabbMin = { 32767, 32767, 32767 },
    .aabbMax = { -32768, -32768, -32768 }
  };
  res.triCount = model.triangles.size();
  res.vertices.reserve(model.triangles.size() * 2);
  res.indices.reserve(model.triangles.size() * 3);

  for(const auto &tri : model.triangles) {
    for(const auto &v : tri.vert) {
      uint32_t idx;
      auto found = res.vertIdxMap.find(v.hash);
      if(found != res.vertIdxMap.end()) {
        idx = found->second;
      } else {
        idx = res.vertices.size();
        if(idx > 0xFFFF) {
          throw std::runtime_error("Too many vertices in object (max 65536): " + model.name);
        }
        res.vertices.push_back(v);
        res.vertIdxMap[v.hash] = idx;
        if(v.boneIndex >= 0)res.hasBones = true;
      }
      res.indices.push_back((uint16_t)idx);
    }
  }

  for(const auto &v : res.vertices) {
    res.aabbMin[0] = std::min(res.aabbMin[0], v.pos[0]);
    res.aabbMin[1] = std::min(res.aabbMin[1], v.pos[1]);
    res.aabbMin[2] = std::min(res.aabbMin[2], v.pos[2]);

    res.aabbMax[0] = std::max(res.aabbMax[0], v.pos[0]);
    res.aabbMax[1] = std::max(res.aabbMax[1], v.pos[1]);
    res.aabbMax[2] = std::max(res.aabbMax[2], v.pos[2]);
  }

  return res;
}

