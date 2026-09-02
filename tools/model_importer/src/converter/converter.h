/**
* @copyright 2024 - Max Bebök
* @license MIT
*/

#pragma once

#include "../math/mat4.h"
#include "../structs.h"

void convertVertex(
  float modelScale, float texSizeX, float texSizeY, const V64M::VertexNorm &v, V64M::VertexOut &vT3D,
  const Mat4 &mat, const std::vector<Mat4> &matrices, bool uvAdjust
);
V64M::ModelFlat flattenModel(const V64M::Model& model);

void convertAnimation(V64M::Anim &anim, const std::unordered_map<std::string, const V64M::Bone*> &nodeMap);