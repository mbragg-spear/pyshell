import configparser

# --- WORKAROUND FOR PYTHON 3.12+ ---
# Restore SafeConfigParser (which was removed) as an alias for ConfigParser
if not hasattr(configparser, 'SafeConfigParser'):
    configparser.SafeConfigParser = configparser.ConfigParser
# -----------------------------------

from setuptools import setup, find_packages, Extension
import sys

# Define the C Extension
shell_extension = Extension(
    'shell_core',
    sources=['src/shell_core.c'], # Add all your C files here
)


setup(
    name="shellhost",
    version="2.0.0",
    description="Turn Python functions into interactive shell commands in an isolated environment.",
    long_description="Provides an isolated interactive shell environment that you can import Python functions into as shell commands.",
    author="M. Bragg",
    author_email="mbragg@spear.ai",
    url="https://github.com/mbragg-spear/pyshell",
    ext_modules=[shell_extension],
    packages=find_packages(where='src'),
    package_dir={'': 'src'}
)
