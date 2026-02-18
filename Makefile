# MASTER MAKEFILE for Cross-Platform Distribution

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
	sudo apt-get update
	sudo apt-get install -y python3-stdeb debhelper python3-all python3-dev build-essential

build:
	rm -rf deb_dist dist build
	python3 setup.py --command-packages=stdeb.command bdist_deb

install: deps build
	sudo dpkg -i deb_dist/*.deb || sudo apt-get install -f -y

reinstall: clean deps build
	-sudo apt remove python3-shellhost -y
	sudo dpkg -i deb_dist/*.deb || sudo apt-get install -f -y


endif

# --- FEDORA / RHEL / CENTOS (dnf + .rpm) ---
ifeq ($(IS_RHEL),yes)
deps:
	sudo dnf install -y rpm-build python3-devel gcc redhat-rpm-config

build:
	rm -rf dist build
	# bdist_rpm creates the spec file and the rpm automatically
	python3 setup.py bdist_rpm

install: deps build
	# Find the generated RPM (usually in dist/) and install it
	sudo dnf install -y dist/*.noarch.rpm dist/*.x86_64.rpm
endif

# --- MACOS (Homebrew) ---
ifeq ($(OS),Darwin)
deps:
	# Check if brew is installed
	@which brew > /dev/null || (echo "Homebrew required. Visit brew.sh"; exit 1)

build:
	# 1. Get absolute path of current directory
	$(eval CUR_DIR := $(shell pwd))
	# 2. Create a temporary Formula file pointing to this directory
	sed 's|CURRENT_DIR|$(CUR_DIR)|g' Formula.rb.in > local_formula.rb

install: build
	# Install using the local formula in verbose mode to show compilation
	brew install --build-from-source ./local_formula.rb
	# Clean up
	rm local_formula.rb
endif

# --- CLEANUP ---
clean:
	rm -rf deb_dist dist build *.egg-info shellhost*.rb
	rm -rf *.tar.gz *.rpm
