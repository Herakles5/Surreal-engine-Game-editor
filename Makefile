# ==============================================================================
# Surreal 3D Engine & Game Studio - Build System
# ==============================================================================

CXX ?= g++
CXXFLAGS ?= -O3 -mavx2 -mfma -fopenmp -pthread -std=c++17 -Wall -Wextra -Wno-unused-parameter
LDFLAGS ?= -lGL -lSDL2 -ldl -lm -fopenmp

TARGET_STUDIO = surreal-editor
TARGET_BENCH = surreal-bench
TARGET_PHOTO = surreal-photo3d

SRCS_STUDIO = surreal_live_dojo.cpp
SRCS_BENCH = test_surreal_benchmark.cpp
SRCS_PHOTO = photo3d_preview.cpp

all: $(TARGET_STUDIO) $(TARGET_BENCH) $(TARGET_PHOTO)

$(TARGET_STUDIO): $(SRCS_STUDIO)
	@echo "[BUILD] Compiling Surreal 3D Studio..."
	$(CXX) $(CXXFLAGS) $(SRCS_STUDIO) $(LDFLAGS) -o $(TARGET_STUDIO)
	@echo "[✓] $(TARGET_STUDIO) built successfully."

$(TARGET_BENCH): $(SRCS_BENCH)
	@echo "[BUILD] Compiling Surreal Benchmark Suite..."
	$(CXX) $(CXXFLAGS) $(SRCS_BENCH) $(LDFLAGS) -o $(TARGET_BENCH)
	@echo "[✓] $(TARGET_BENCH) built successfully."

$(TARGET_PHOTO): $(SRCS_PHOTO) surreal_image3d.hpp
	@echo "[BUILD] Compiling Photo-to-3D preview tool..."
	$(CXX) $(CXXFLAGS) $(SRCS_PHOTO) $(LDFLAGS) -o $(TARGET_PHOTO)
	@echo "[✓] $(TARGET_PHOTO) built successfully."

install: all
	@echo "[INSTALL] Installing Surreal Engine & Studio to /usr/local..."
	sudo ./install_surreal.sh

clean:
	rm -f $(TARGET_STUDIO) $(TARGET_BENCH) $(TARGET_PHOTO) *.o

.PHONY: all clean install

