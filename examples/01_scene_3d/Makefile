BUILD_DIR = build

# Path to the engine64 checkout. Set it to wherever the engine lives; this
# example sits two levels down inside the repo.
ENGINE_DIR = ../..

include $(N64_INST)/include/n64.mk

N64_CFLAGS += -std=gnu2x -I$(ENGINE_DIR)/include -I$(ENGINE_DIR)/src

# The engine's own model importer (host tool, built on demand).
MODEL_IMPORTER = $(ENGINE_DIR)/tools/model_importer/gltf_to_model

# --asset-path: the importer looks every material's texture up by FILE NAME,
# recursively, under this path, and builds the path left in the .model by
# replacing this prefix with "rom:/".
GLTF_FLAGS = '--base-scale=1' '--asset-path=assets'

PROJECT_NAME = 01_scene_3d

# --- sources ------------------------------------------------------------------

include $(ENGINE_DIR)/engine.mk

src = $(wildcard *.c)

objects = $(engine_src:%.c=$(BUILD_DIR)/engine/%.o) \
          $(src:%.c=$(BUILD_DIR)/%.o)

# --- assets -------------------------------------------------------------------

assets_png  = $(wildcard assets/textures/*.png)
assets_gltf = $(wildcard assets/models/*.glb)

assets_conv = $(addprefix filesystem/textures/,$(notdir $(assets_png:%.png=%.sprite))) \
              $(addprefix filesystem/models/,$(notdir $(assets_gltf:%.glb=%.model)))


all: $(PROJECT_NAME).z64

$(MODEL_IMPORTER):
	$(MAKE) -C $(ENGINE_DIR)/tools/model_importer

filesystem/textures/%.sprite: assets/textures/%.png
	@mkdir -p $(dir $@)
	@echo "    [SPRITE] $@"
	$(N64_MKSPRITE) $(MKSPRITE_FLAGS) -o filesystem/textures "$<"

filesystem/models/%.model: assets/models/%.glb $(MODEL_IMPORTER)
	@mkdir -p $(dir $@)
	@echo "    [MODEL] $@"
	$(MODEL_IMPORTER) $(GLTF_FLAGS) "$<" $@
	$(N64_BINDIR)/mkasset -c 2 -o filesystem/models $@

$(BUILD_DIR)/$(PROJECT_NAME).dfs: $(assets_conv)
$(BUILD_DIR)/$(PROJECT_NAME).elf: $(objects)

$(PROJECT_NAME).z64: N64_ROM_TITLE="01_scene_3d"
$(PROJECT_NAME).z64: $(BUILD_DIR)/$(PROJECT_NAME).dfs

clean:
	rm -rf $(BUILD_DIR) *.z64
	rm -rf filesystem

-include $(objects:%.o=%.d)

.PHONY: all clean
