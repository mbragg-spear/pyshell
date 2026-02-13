import configparser

# --- WORKAROUND FOR PYTHON 3.12+ ---
# Restore SafeConfigParser (which was removed) as an alias for ConfigParser
if not hasattr(configparser, 'SafeConfigParser'):
    configparser.SafeConfigParser = configparser.ConfigParser
# -----------------------------------

from setuptools import setup, find_packages, Extension
import sys

shell_extension = Extension(
    'shellparser',                # The name of the module
    sources=['src/shellparser.c'] # The list of C files to compile
)

setup(
    name="shellhost",
    version="1.0.3",
    description="Turn Python functions into interactive shell commands in an isolated environment.",
    long_description="Provides an isolated interactive shell environment that you can import Python functions into as shell commands.",
    author="M. Bragg",
    author_email="mbragg@spear.ai",
    url="https://github.com/mbragg-spear/pyshell",
    ext_modules=[shell_extension],
    packages=find_packages(where='src'),
    package_dir={'': 'src'}
)
