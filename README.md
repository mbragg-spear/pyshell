# \# pyshell

# PyShell is a small C + Python library that simplifies the creation of shell style apps within Python by allowing function definitions to be initialized directly as shell commands.

# 

# This library is compatible with Windows, Linux, and MacOS systems.

# 

# 

# \## Full README coming soon!

# The full README is still a work in progress, as is this library. More documentation to come!

# 

# 

# \## Getting Started

# This section will cover how to get this library installed on your system.

# 

# \### Requirements

# \- python3

# \- pip

# \- gcc

# \- make

# 

# \### Installation

# 

# 1\. Clone the repository

# ```

# git clone https://github.com/mbragg-spear/pyshell.git

# cd pyshell

# ```

# 

# 2\. Run the installation

# ```

# make

# make install

# ```

# 

# 

# \### Usage

# In any Python application where you'd want to make an interactive shell, simply `import pyshell` to get started.

# 

# PyShell supports basic variable assignment/expansion with `sh` like syntax, as well as an accessible command history with up and down arrow keys.

# 

# You can see some examples of this in the \[examples](#Examples) section.

# 

# \### Examples

# 

# \#### Example 1: Creating a PyShell Command with a function declaration.

# The following code snippet creates a function `add\_five` which is initialized as a PyShell Command.

# 

# ```

# \#!/usr/bin/env python3

# 

# import pyshell

# 

# @pyshell.Command.decorator

# def add\_five(x: int) -> int: # The decorator takes care of the configuration.

# &nbsp; """ Adds five to the input number.

# 

# &nbsp; Args:

# &nbsp;   x: The numerical value to add 5 to.

# 

# &nbsp; Returns:

# &nbsp;   The original x value plus five.

# 

# &nbsp; Raises:

# &nbsp;   TypeError: If x is not a type that supports the + operator.

# &nbsp; """

# 

# &nbsp; answer = x + 5

# &nbsp; 

# &nbsp; print(answer) # Return values are stored in the variable $? so print answer to screen and return it.

# 

# &nbsp; return answer

# 

# my\_shell = pyshell.PyShell()

# 

# my\_shell.add\_command(add\_five)

# 

# my\_shell.open() # or just my\_shell()

# ```

# 

# After running this, the interactive interface will open with a handful of builtin commands, as well as the `add\_five` command.

# 

# ```

# pyshell> help

# Command: help

# Command: exit

# Command: env

# Command: echo

# Command: add\_five

# 

# pyshell> help add\_five

# \*\*\* docstring for add\_five gets printed here \*\*\*

# 

# pyshell> add\_five 10

# 15

# 

# pyshell> echo $?

# 15

# ```

# 

# 

# \### Example 2: Variable Assignment and Expansion

# This code snippet utilizes the same `add\_five` function defined in Example 1.

# 

# ```

# pyshell> MY\_VAR=15

# pyshell> echo $MY\_VAR

# 15

# pyshell> add\_five $MY\_VAR

# 20

# pyshell> echo $?

# 20

# ```

