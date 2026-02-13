# 1. Detect the Operating System
# If output is empty, we assume Linux/Unix
ifeq ($(OS),Windows_NT)
    DETECTED_OS := Windows
else
    DETECTED_OS := $(shell uname -s)
endif

# 2. Set Defaults (Linux/Mac)
CC = gcc
CFLAGS = -fPIC -Wall -Wextra -O2 -m64
LDFLAGS = -shared
RM = rm -rf
TARGET_LIB = pyshell/libshellparser.so

# 3. Override for Windows
ifeq ($(DETECTED_OS),Windows)
	# Recommended: Use MinGW gcc if available (easier compatibility with existing flags)
	CC = gcc

  # Windows uses .dll, not .so
	TARGET_LIB = pyshell\libshellparser.dll

	# Windows command to delete files
	RM = del /f /s /q

	# Remove -fPIC (not needed/supported on Windows generally)
	CFLAGS = -Wall -Wextra -O2
endif

# 4. Standard Rules
SRC = shellparser.c

all: $(TARGET_LIB)

$(TARGET_LIB): $(SRC)
	@echo [1/2] Building for $(DETECTED_OS)...
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRC) -o $(TARGET_LIB)
	@echo Compiled $(TARGET_LIB) successfully.

clean:
	$(RM) $(TARGET_LIB)
	$(RM) build pyshell.egg-info
	@echo Cleaned up build artifacts.

install: $(TARGET_LIB)
	@echo [2/2] Installing python package...
	python3 -m pip install . --break-system-packages
	@echo Cleaning up...
	$(RM) build pyshell.egg-info

.PHONY: all clean install
