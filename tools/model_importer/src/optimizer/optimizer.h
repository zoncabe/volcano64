/**
* @copyright 2024 - Max Bebök
* @license MIT
*/

#pragma once
#include "../structs.h"

namespace V64M
{
  std::vector<int16_t> createMeshBVH(const std::vector<ModelFlat> &models);
}