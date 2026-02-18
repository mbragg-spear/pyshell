# MASTER MAKEFILE for Cross-Platform Distribution

ifeq ($(OS),Windows_NT)
        @echo '[ERROR] Use "pip install shellhost" to install on Windows systems.'
        exit 1
endif

# --- OS DETECTION ---
OS := $(shell uname -s)
ifeq ($(OS),Linux)
        # Check if Debian-based (uses apt) or RHEL-based (uses dnf/yum)
        IS_DEBIAN := $(shell if [ -f /etc/debian_version ]; then echo yes; else echo no; fi)
        IS_RHEL := $(shell if [ -f /etc/redhat-release ]; then echo yes; else echo no; fi)
endif

# --- DEFAULT TARGET ---
all: install

# --- DEBIAN / UBUNTU (apt + .deb) ---
ifeq ($(IS_DEBIAN),yes)
deps:
	@echo "[1/3] Installing dependencies..."
	sudo apt-get install -y gcc python3-stdeb debhelper python3-all python3-dev python3-pip build-essential

build:
	@echo "[2/3] Building library..."
	rm -rf deb_dist dist build
	python3 setup.py --command-packages=stdeb.command bdist_deb

install: deps build
	@echo "[3/3] Installing library with apt..."
	sudo dpkg -i deb_dist/*.deb || sudo apt-get install -f -y
	@echo "[DONE] Installation succeeded!"

uninstall:
	@echo "Uninstalling..."
	-sudo apt remove python3-shellhost -y
endif

# --- FEDORA / RHEL / CENTOS (dnf + .rpm) ---
ifeq ($(IS_RHEL),yes)
deps:
	@echo "[1/3] Installing dependencies..."
	sudo dnf install -y rpm-build python3-devel python3-pip gcc redhat-rpm-config

build:
	@echo "[2/3] Building library..."
	rm -rf dist build
	# bdist_rpm creates the spec file and the rpm automatically
	python3 setup.py bdist_rpm

install: deps build
	@echo "[3/3] Installing library with dnf..."
	# Find the generated RPM (usually in dist/) and install it
	sudo dnf install -y dist/*.noarch.rpm dist/*.x86_64.rpm
	@echo "[DONE] Installation succeeded!"
uninstall:
	@echo "Uninstalling..."
	-sudo dnf erase python3-shellhost -y
endif

# --- MACOS (Homebrew) ---
ifeq ($(OS),Darwin)
deps:
	# Check if brew is installed
	@which brew > /dev/null || (echo "Homebrew required. Visit brew.sh"; exit 1)

	@brew tap | grep -q "^user/local-dev$$" || brew tap-new user/local-dev --no-git

build:
	# 1. Get absolute path of current directory
	$(eval CUR_DIR := $(shell pwd))

	tar --exclude='.git' --exclude='shellhost.tar.gz' -czf shellhost.tar.gz .

	# 2. Create a temporary Formula file pointing to this directory
	sed 's|CURRENT_DIR|$(CUR_DIR)|g' Formula.rb.in > shellhost.rb

install: deps build
	cp shellhost.rb $$(brew --repo user/local-dev)/Formula/shellhost.rb

	# Install using the local formula in verbose mode to show compilation
	brew install --build-from-source user/local-dev/shellhost || brew upgrade user/local-dev/shellhost
	# Clean up
	rm shellhost.rb shellhost.tar.gz
endif

# --- CLEANUP ---
clean:
	rm -rf deb_dist dist build shellhost*.rb *.tar.gz *.rpm
	rm -rf src/shellhost.egg-info
	rm -rf src/shellhost/__pycache__
