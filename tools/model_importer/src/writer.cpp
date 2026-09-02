/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include <stdio.h>
#include <string>
#include <filesystem>
#include <algorithm>
#include <cassert>

#include "structs.h"
#include "hash.h"
#include "args.h"

#include "binaryFile.h"
#include "converter/converter.h"
#include "optimizer/optimizer.h"

namespace fs = std::filesystem;

namespace {
  uint32_t insertString(std::string &stringTable, const std::string &newString) {
    auto strPos = stringTable.find(newString + '\0');
    if(strPos == std::string::npos) {
      strPos = stringTable.size();
      stringTable += newString;
      stringTable.push_back('\0');
    }
    return strPos;
  }

  int writeBone(BinaryFile &file, const V64M::Bone &bone, std::string &stringTable, float globalScale, int level) {
    //printf("Bone[%d]: %s -> %d\n", bone.index, bone.name.c_str(), bone.parentIndex);

    file.write(insertString(stringTable, bone.name));
    file.write<uint16_t>(bone.parentIndex);
    file.write<uint16_t>(level); // level

    auto normPos = bone.pos * globalScale;
    file.writeArray(bone.scale.data, 3);
    file.writeArray(bone.rot.data, 4);
    file.writeArray(normPos.data, 3);

    int boneCount = 1;
    for(const auto& child : bone.children) {
      boneCount += writeBone(file, *child, stringTable, globalScale, level+1);
    }
    return boneCount;
  };

  std::string getRomPath(const std::string &path) {
    std::string basDir = "filesystem/";
    auto fsPos = path.find(basDir);
    if(fsPos != std::string::npos) {
      return std::string("rom:/") + path.substr(fsPos + basDir.length());
    }
    return path;
  }

  std::string getStreamDataPath(const char* filePath, uint32_t idx) {
    auto sdataPath = std::string(filePath).substr(0, std::string(filePath).size()-5);
    std::replace(sdataPath.begin(), sdataPath.end(), '\\', '/');
    return sdataPath + "." + std::to_string(idx) + ".sdata";
  }
}

void V64M::writeModel(
  const Config &config,
  const ModelData &modelData,
  const std::string &outPath,
  const std::vector<CustomChunk> &customChunks
)
{
  int16_t aabbMin[3] = {32767, 32767, 32767};
  int16_t aabbMax[3] = {-32768, -32768, -32768};

  uint32_t chunkIndex = 0;
  uint32_t chunkCount = 2; // vertices + indices
  if(config.createBVH)chunkCount += 1;
  chunkCount += modelData.materials.size();
  chunkCount += customChunks.size();

  bool anyBones = false;
  std::vector<ModelFlat> modelFlats{};
  modelFlats.reserve(modelData.models.size());
  for(const auto & model : modelData.models) {
    auto flat = flattenModel(model);
    if(config.verbose) {
      printf("[%s] Vertices out: %ld | Indices: %ld\n",
        model.name.c_str(), flat.vertices.size(), flat.indices.size());
    }

    anyBones |= flat.hasBones;
    modelFlats.push_back(flat);
    chunkCount += 1; // object

    aabbMin[0] = std::min(aabbMin[0], flat.aabbMin[0]);
    aabbMin[1] = std::min(aabbMin[1], flat.aabbMin[1]);
    aabbMin[2] = std::min(aabbMin[2], flat.aabbMin[2]);

    aabbMax[0] = std::max(aabbMax[0], flat.aabbMax[0]);
    aabbMax[1] = std::max(aabbMax[1], flat.aabbMax[1]);
    aabbMax[2] = std::max(aabbMax[2], flat.aabbMax[2]);
  }
  if(anyBones)chunkCount += 1; // per-vertex bone indices
  chunkCount += modelData.skeletons.empty() ? 0 : 1;
  chunkCount += modelData.animations.size();

  std::vector<BinaryFile> streamFiles{};

  // Main file
  BinaryFile file{};
  file.writeChars("V64", 3);
  file.write<uint8_t>(V64M::MODEL_VERSION);
  file.write(chunkCount); // chunk count

  file.write<uint16_t>(0); // total vertex count (set later)
  file.write<uint16_t>(0); // total index count (set later)

  uint32_t offsetChunkTypeTable = file.getPos();
  file.skip(3 * sizeof(uint32_t)); // chunk type indices (filled later)

  uint32_t offsetStringTablePtr = file.getPos();
  file.skip(sizeof(uint32_t)); // string table offset (filled later)

  file.write<uint32_t>(0); // block, set by users at runtime
  file.writeArray(aabbMin, 3);
  file.writeArray(aabbMax, 3);

  uint32_t offsetChunkTable = file.getPos();
  file.skip(chunkCount * sizeof(uint32_t)); // chunk-table

  auto addToChunkTable = [&](char type) {
    uint32_t offset = file.posPush();
      file.setPos(offsetChunkTable);
      file.writeChunkPointer(type, offset);
      offsetChunkTable = file.getPos();
    file.posPop();
    ++chunkIndex;
  };

  auto addChunkTypeIndex = [&]() {
    file.posPush();
      file.setPos(offsetChunkTypeTable);
      file.write(chunkIndex);
      offsetChunkTypeTable = file.getPos();
    file.posPop();
  };

  // Chunks
  BinaryFile chunkVerts{};
  BinaryFile chunkIndices{};
  BinaryFile chunkBVH{};
  std::vector<std::shared_ptr<BinaryFile>> chunkMaterials{};
  std::vector<BinaryFile> chunkSkeletons{};

  std::string stringTable = "S";

  // now write out each model (aka. collection of mesh-parts + materials)
  int m=0;
  uint16_t totalVertCount = 0;
  uint16_t totalIndexCount = 0;

  if(!modelData.skeletons.empty())
  {
    auto &chunkBone = chunkSkeletons.emplace_back();
    chunkBone.skip(4); // size, filed later

    int boneCount = 0;
    for(auto &skel : modelData.skeletons) {
      boneCount += writeBone(chunkBone, skel, stringTable, config.globalScale, 0);
    }

    chunkBone.setPos(0);
    chunkBone.write<uint16_t>(boneCount);
  }

  if(config.createBVH) {
    auto bvhData = createMeshBVH(modelFlats);
    chunkBVH.writeArray(bvhData.data(), bvhData.size());
  }

  // write used materials
  std::unordered_map<std::string, uint32_t> materialMap{};
  for(auto &material_ : modelData.materials)
  {
    auto &material = material_.second;
    uint32_t matIdx = materialMap.size();
    materialMap[material.name] = matIdx;
    auto f = std::make_shared<BinaryFile>();

    if(config.materialWriter && config.materialWriter(f, material, matIdx)) {
      chunkMaterials.push_back(f);
      continue;
    }

    f->write(material.colorCombiner);
    f->write(material.otherModeValue);
    f->write(material.otherModeMask);
    f->write(material.blendMode);
    f->write(material.drawFlags);

    f->write<uint8_t>(0);
    f->write(material.fogMode);
    f->write<uint8_t>(
      material.setPrimColor |
      (material.setEnvColor << 1) |
      (material.setBlendColor << 2)
    );
    f->write(material.vertexFxFunc);

    f->writeArray(material.primColor, 4);
    f->writeArray(material.envColor, 4);
    f->writeArray(material.blendColor, 4);
    f->write(insertString(stringTable, material.name));

    std::vector materials{&material.texA, &material.texB};
    for(const MaterialTexture* mat_ : materials) {
      const MaterialTexture&mat = *mat_;

      f->write(mat.texReference);

      if(!mat.texPathRom.empty()) {
        // check if string already exits
        auto strPos = insertString(stringTable, mat.texPathRom);

        uint32_t hash = stringHash(mat.texPathRom);
        //printf("Texture: %s (%d)\n", texPath.c_str(), hash);
        f->write((uint32_t)strPos);
        f->write(hash);

      } else {
        f->write(0);
        // if no texture is set, use the reference as hash
        // this is needed to force a reevaluation of the texture state
        f->write(mat.texReference);
      }

      f->write((uint32_t)0); // runtime pointer
      f->write((uint16_t)mat.texWidth);
      f->write((uint16_t)mat.texHeight);

      auto writeTile = [&](const V64M::TileParam &tile) {
        f->write(tile.low);
        f->write(tile.high);
        f->write(tile.mask);
        f->write(tile.shift);
        f->write(tile.mirror);
        f->write(tile.clamp);
      };
      writeTile(mat.s);
      writeTile(mat.t);
    }

    chunkMaterials.push_back(f);
  }

  file.align(8);
  BinaryFile chunkBoneIndices{};
  for(auto &model : modelData.models)
  {
    file.align(4);
    addToChunkTable('O');
    uint32_t matIdx = materialMap[model.materialName];

    // write object chunk
    const auto &flat = modelFlats[m];
    file.write(insertString(stringTable, model.name));
    file.write((uint32_t)flat.vertices.size());
    file.write((uint32_t)flat.indices.size());
    file.write(matIdx);
    file.write<uint32_t>(0); // block, set at runtime
    file.write<uint32_t>(0); // visibility, set at runtime + padding
    file.writeArray(flat.aabbMin, 3);
    file.writeArray(flat.aabbMax, 3);

    // byte offsets into the vertex/index/bone-index chunks, patched to
    // pointers at load time
    file.write((uint32_t)chunkVerts.getPos());
    file.write((uint32_t)chunkIndices.getPos());
    file.write(flat.hasBones ? (uint32_t)chunkBoneIndices.getPos() : (uint32_t)0xFFFFFFFF);

    // vertex buffer: plain, one vertex per 16 bytes, in the layout the
    // vertex pipeline is created with
    for(const auto &vert : flat.vertices)
    {
      chunkVerts.write(vert.pos[0]);
      chunkVerts.write(vert.pos[1]);
      chunkVerts.write(vert.pos[2]);
      chunkVerts.write(vert.norm);
      chunkVerts.write(vert.rgba);
      chunkVerts.write(vert.s);
      chunkVerts.write(vert.t);
    }

    // one unsigned 8-bit matrix index per vertex, parallel to the buffer
    if(flat.hasBones) {
      for(const auto &vert : flat.vertices) {
        chunkBoneIndices.write((uint8_t)(vert.boneIndex < 0 ? 0 : vert.boneIndex));
      }
    }

    // index buffer: plain u16 triangle list
    chunkIndices.writeArray(flat.indices.data(), flat.indices.size());

    totalVertCount += flat.vertices.size();
    totalIndexCount += flat.indices.size();

    ++m;
  }

  uint16_t animIdx = 0;
  for(const auto &anim : modelData.animations) {
    BinaryFile streamFile{};
    file.align(4);
    addToChunkTable('A');

    file.write(insertString(stringTable, anim.name));
    file.write<float>(anim.duration);
    file.write<uint32_t>(anim.keyframes.size());
    file.write<uint16_t>(anim.channelCountQuat);
    file.write<uint16_t>(anim.channelCountScalar);
    file.write<uint32_t>(insertString(stringTable,
      getRomPath(getStreamDataPath(outPath.c_str(), animIdx))
    ));

    std::unordered_set<uint32_t> channelHasKF{};
    for(int k=0; k<anim.keyframes.size(); ++k) {
      bool isLastKF = (k >= anim.keyframes.size()-1);
      const auto &kf = anim.keyframes[k];
      const auto &kfNext = isLastKF ? kf : anim.keyframes[k+1];

      bool nextIsLarge = kfNext.valQuantSize > 1;

      uint16_t timeNext = kf.timeNextInChannelTicks;
      assert(timeNext < (1 << 15)); // prevent conflicts with size flag
      if(nextIsLarge)timeNext |= (1 << 15); // encode size of the next KF here

      //printf("KF[%d]: %.4f, needed: %.4f, next: %.4f\n", k, kf.time, kf.timeNeeded, kf.timeNextInChannel);

      streamFile.write<uint16_t>(timeNext);
      streamFile.write<uint16_t>(kf.chanelIdx);
      for(int v=0; v<kf.valQuantSize; ++v) {
        streamFile.write<uint16_t>(kf.valQuant[v]);
      }

      // force the first keyframe to have 2 values, this is to have a known initial state
      if(k == 0 && kf.valQuantSize == 1) {
        streamFile.write<uint16_t>(0);
      }
    }
    streamFiles.push_back(streamFile);

    for(const auto &ch : anim.channelMap) {
      file.write(ch.targetIdx);
      file.write(ch.targetType);
      file.write(ch.attributeIdx);
      file.write((ch.valueMax - ch.valueMin) / (float)0xFFFF);
      file.write(ch.valueMin);
    }

    ++animIdx;
  }

  // Now patch all chunks together and write out the chunk-table

  if(config.createBVH) {
    file.align(8);
    addToChunkTable('B');
    file.writeMemFile(chunkBVH);
  }

  file.align(16);
  addChunkTypeIndex();
  addToChunkTable('V');
  file.writeMemFile(chunkVerts);

  file.align(4);
  addChunkTypeIndex();
  addToChunkTable('I');
  file.writeMemFile(chunkIndices);

  if(anyBones) {
    file.align(8);
    addToChunkTable('J');
    file.writeMemFile(chunkBoneIndices);
  }

  addChunkTypeIndex();
  for(auto &f : chunkMaterials) {
    file.align(8);
    addToChunkTable(config.materialWriter ? 'm' : 'M');
    file.writeMemFile(*f);
  }

  for(const auto &chunkSkel : chunkSkeletons) {
    file.align(8);
    addToChunkTable('S');
    file.writeMemFile(chunkSkel);
  }

  for(const auto &custom : customChunks) {
    file.align(8);
    addToChunkTable(custom.type);
    file.writeArray(custom.data.data(), custom.data.size());
  }

  // String table
  file.align(4);
  uint32_t stringTableOffset = file.getPos();
  file.write(stringTable);

  file.setPos(offsetStringTablePtr);
  file.write(stringTableOffset);

  // patch vertex/index count
  file.setPos(0x08);
  file.write(totalVertCount);
  file.write(totalIndexCount);

  // write to actual file
  file.writeToFile(outPath.c_str());

  for(int s=0; s<streamFiles.size(); ++s) {
    auto sdataPath = getStreamDataPath(outPath.c_str(), s);
    streamFiles[s].writeToFile(sdataPath.c_str());
  }
}