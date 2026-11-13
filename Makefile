# ==============================================================================
# Surreal 3D Engine & Game Studio - Build System
# ==============================================================================

CXX ?= g++
CXXFLAGS ?= -O2 -mavx2 -mfma -fopenmp -pthread -std=c++20 -DSURREAL_EXTERNAL_THIRD_PARTY_IMPL -Wall -Wextra -Wno-unused-parameter
LDFLAGS ?= -lGL -lSDL2 -ldl -lm -fopenmp

TARGET_STUDIO = surreal-editor
TARGET_BENCH = surreal-bench
TARGET_PHOTO = surreal-photo3d
TARGET_SUITE = test_surreal_suite
TARGET_GLTF = test_surreal_gltf

SRCS_STUDIO = surreal_live_dojo.cpp
SRCS_BENCH = test_surreal_benchmark.cpp
SRCS_PHOTO = photo3d_preview.cpp
SRCS_SUITE = test_surreal_suite.cpp
SRCS_GLTF = test_surreal_gltf.cpp
THIRD_PARTY_OBJ = surreal_third_party.o

all: $(TARGET_STUDIO) $(TARGET_BENCH) $(TARGET_PHOTO) $(TARGET_SUITE) $(TARGET_GLTF)

$(THIRD_PARTY_OBJ): surreal_third_party.cpp cgltf.h stb_image.h
	@echo "[BUILD] Compiling Shared Third-Party Objects (cgltf + stb_image)..."
	$(CXX) -O2 -std=c++20 -c surreal_third_party.cpp -o $(THIRD_PARTY_OBJ)

$(TARGET_STUDIO): $(SRCS_STUDIO) $(THIRD_PARTY_OBJ) *.hpp
	@echo "[BUILD] Compiling Surreal 3D Studio..."
	$(CXX) $(CXXFLAGS) $(SRCS_STUDIO) $(THIRD_PARTY_OBJ) $(LDFLAGS) -o $(TARGET_STUDIO)
	@echo "[✓] $(TARGET_STUDIO) built successfully."

$(TARGET_BENCH): $(SRCS_BENCH) $(THIRD_PARTY_OBJ) *.hpp
	@echo "[BUILD] Compiling Surreal Benchmark Suite..."
	$(CXX) $(CXXFLAGS) $(SRCS_BENCH) $(THIRD_PARTY_OBJ) $(LDFLAGS) -o $(TARGET_BENCH)
	@echo "[✓] $(TARGET_BENCH) built successfully."

$(TARGET_PHOTO): $(SRCS_PHOTO) $(THIRD_PARTY_OBJ) surreal_image3d.hpp surreal_engine.hpp
	@echo "[BUILD] Compiling Photo-to-3D preview tool..."
	$(CXX) $(CXXFLAGS) $(SRCS_PHOTO) $(THIRD_PARTY_OBJ) -lm -pthread -fopenmp -o $(TARGET_PHOTO)
	@echo "[✓] $(TARGET_PHOTO) built successfully."

$(TARGET_SUITE): $(SRCS_SUITE) $(THIRD_PARTY_OBJ) *.hpp
	@echo "[BUILD] Compiling Headless Verification Suite..."
	$(CXX) $(CXXFLAGS) $(SRCS_SUITE) $(THIRD_PARTY_OBJ) -lm -pthread -fopenmp -o $(TARGET_SUITE)
	@echo "[✓] $(TARGET_SUITE) built successfully."

$(TARGET_GLTF): $(SRCS_GLTF) $(THIRD_PARTY_OBJ) *.hpp
	@echo "[BUILD] Compiling glTF Verification Tool..."
	$(CXX) $(CXXFLAGS) $(SRCS_GLTF) $(THIRD_PARTY_OBJ) -lm -pthread -fopenmp -o $(TARGET_GLTF)
	@echo "[✓] $(TARGET_GLTF) built successfully."

test: $(TARGET_SUITE) $(TARGET_GLTF)
	./$(TARGET_SUITE)
	./$(TARGET_GLTF)

install: all
	@echo "[INSTALL] Installing Surreal Engine & Studio to /usr/local..."
	./install_surreal.sh

clean:
	rm -f $(TARGET_STUDIO) $(TARGET_BENCH) $(TARGET_PHOTO) $(TARGET_SUITE) $(TARGET_GLTF) *.o

.PHONY: all test clean install
