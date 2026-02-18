# shellhost.py
import sys
import os
import pydoc
pydoc.pager = pydoc.plainpager # Disable pager for the help() command

import shell_core # This imports the compiled C extension
from .shellhost_command import Command

def register_command(name, func):
    shell_core.register(name, func)

def start(args=None, prompt="shell> "):
    """
    Starts the C-based interactive shell.
    Args:
        args: List of string arguments (defaults to sys.argv)
        prompt: The command prompt string.
    """
    if args is None:
        args = sys.argv

    # Pass control to C. This blocks until the user types 'exit'.
    return shell_core.start(args, prompt)

@Command.auto_command
def echo(*args):
  """ Prints whatever the user input is.

  Args:
    user_input: The input provided by the user on the command line.

  Returns:
    None
  """
  print(*args)


@Command.auto_command
def env():
  """ Prints all currently stored environment variables.

  Args:
    None

  Returns:
    None
  """
  for k,v in os.environ.items():
    print(f"{k}={v}")

@Command.command # Use the non-basic decorator for _help so we can set its command name.
def _help(cmd_name: str = None):
  """ Prints help messages for commands.

  Args:
    -c|--cmd_name: The name of the command specifically to view a help message for.

  Returns:
    1: If the user requested a specific command that could not be located.
    0: Otherwise
  """

  if cmd_name is not None:
    user_func = shell_core.get_command(cmd_name)
    if user_func is None:
      print(f"Error - help: Command {cmd_name} not found.")
      return 1

    target = getattr(user_func, 'func', user_func)
    help(target)

  else:
    # Ask C for the list of names
    cmds = shell_core.get_registry()
    print("Available Commands:")
    print("  " + "\n  ".join(sorted(cmds)))


_help.set_name("help")
_help.add_arg("command-name", optional=True, dtype=str)
shell_core.register("help", _help)
