# shellhost

Shellhost is a small C + Python library that simplifies the creation of shell style apps within Python by allowing function definitions to be initialized directly as shell commands.
 
This library is compatible with Windows, Linux, and MacOS systems. 
 
## Getting Started
This section will cover how to get this library installed on your system.


### pip
To install this in pip-managed systems, you can just `pip install shellhost`.

#### Known Issues
* `externally-managed-environment`
  If you attempt to use pip for this installation on a system that uses an external package manager for  
  Python libraries (`apt`, `yum`, `dnf`, `homebrew`) you will encounter an `externally-managed-environment` error.  
  If you see this error, proceed to the [Non-pip Installation](#non-pip-installation) section.

### Non-pip Installation
If you are on a system with externally managed packages, then follow the instructions below.

#### Requirements
* Your system's package manager (`apt`, `dnf`, `homebrew`).
* `git`

1. Clone the repository
```
git clone https://github.com/mbragg-spear/shellhost.git
cd shellhost
```

2. Run the installation
```
make
make install
```

During the `make install` several dependencies will be installed from your package manager.

### Usage
In any Python application where you'd want to make an interactive shell, simply `import shellhost` to get started. 

Shellhost supports basic variable assignment/expansion with `sh` like syntax, as well as an accessible command history with up and down arrow keys. 

You can see some examples of this in the [examples](#Examples) section. 

### Examples

#### Example 1: Creating a Shellhost Command with a function declaration.
The following code snippet creates a function `add_five` which is initialized as a Shellhost Command.

```
#!/usr/bin/env python3
import shellhost 
@shellhost.Command.decorator
def add_five(x: int) -> int: # The decorator takes care of the configuration.
  """ Adds five to the input number. 
  Args:
    x: The numerical value to add 5 to.

  Returns:
    The original x value plus five.

  Raises:
    TypeError: If x is not a type that supports the + operator.
  """
  answer = x + 5
  print(answer) # Return values are stored in the variable $? so print answer to screen and return it.
  return answer


my_shell = shell.Shell() 
my_shell.add_command(add_five)
my_shell.open() # or just my_shell()
```

After running this, the interactive interface will open with a handful of builtin commands, as well as the `add_five` command.
 
```
shell> help
Command: help
Command: exit
Command: env
Command: echo
Command: add_five

shell> help add_five
*** docstring for add_five gets printed here ***

shell> add_five 10
15
shell> echo $?
15
```

### Example 2: Variable Assignment and Expansion
This code snippet utilizes the same `add_five` function defined in Example 1.

```
shell> MY_VAR=15
shell> echo $MY_VAR
15
shell> add_five $MY_VAR
20
shell> echo $?
20
```



