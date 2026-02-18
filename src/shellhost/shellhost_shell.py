# shellhost __init__.py
import platform
import inspect
import subprocess
import os
import re
import io
import _thread

import shellparser
from .shellhost_command import Command
from .shellhost_ast import *
from .shellhost_io import *

"""
┌───────────────────────────────────────┐
│ ## shellhost ##                       │
│ shellhost aims to provide a simpler   │
│ interface to making command line      │
│ shell-type apps in python.            │
└───────────────────────────────────────┘
"""

class Shell:
  """ Class Object for the actual shell environment.

  """
  def __init__(self, name='shell', prompt='shell> ', include_builtins=True):
    """ Initializes a new Shell object.

    Args:
      name: The string name for your shell. Default: shell (UNUSED)
      prompt: The prompt that will be displayed on the command line. Default: 'shell> '
      include_builtins: Boolean that determines if the built-in shell commands are initialized. Default: True

    Returns:
      A new Shell object.
    """
    self.name = name

    exit_command = Command('exit', self._exit)
    exit_command.add_arg('retcode', default=0, nargs=1)

    help_command = Command('help', self._help)
    help_command.add_arg('command-name', default=None, nargs=1)

    env_command = Command('env', self._env)

    echo_command = Command('__echo__', self._echo)
    echo_command.add_arg('user-input', nargs='*')

    self.commands = {
        'exit': exit_command,
        'help': help_command,
        'env': env_command,
        '__echo__': echo_command
    } if include_builtins == True else {}

    self.error_message = None
    self.name = name
    self.prompt = prompt

    ### Stateful environment tracking
    self.variables = os.environ.copy()
    self.variables['?'] = '0'                # Return values from commands
    self.variables['!'] = str(os.getpid())   # Current PID
    self.jobs = {}                           # Container for background jobs


  def _exit(self, retcode=0):
    """ Safely calls the python exit() function from within the shell.

    Args:
      None

    Returns:
      None
    """
    _thread.interrupt_main()
    sys.exit(retcode)

  def _echo(self, *args):
    """ Prints whatever the user input is.

    Args:
      user_input: The input provided by the user on the command line.

    Returns:
      None
    """
    print(*args)



  def _env(self):
    """ Prints all currently stored environment variables.

    Args:
      None

    Returns:
      None
    """
    for K,V in self.variables.items():
      print(f"{K} = {V}")


  def _help(self, cmd_name=None):
    """ Prints help messages for commands.

    Args:
      cmd_name: The name of the command specifically to view a help message for.

    Returns:
      1: If the user requested a specific command that could not be located.
      0: Otherwise
    """


    if cmd_name is not None:
      user_func = self.commands.get(cmd_name)
      if user_func is None:
        print(f"Error - help: Command {cmd_name} not found.")
        return 1

      if type(user_func) == Command:
        help(user_func.func)
      else:
        help(user_func)

    else:
      for command_name, command_function in self.commands.items():
        print(f"Command: {command_name}")

    print("\n")

  def execute_ast(self, nodes, stdin=None, stdout=None, stderr=None):
    if not nodes: return

    this_job = None # Ensure variable exists even if loop doesn't run

    for node in nodes:

      if isinstance(node, CommandNode):
        if self.handle_variable_assignment(node.arguments[0]):
          return

        # 1. Expand variables and command substitutions first
        current_args = self.expand_variables(node.arguments)
        current_args = self.expand_command_substitutions(current_args)

        actual_stdin = stdin if stdin is not None else node.stdin
        actual_stdout = stdout if stdout is not None else node.stdout
        actual_stderr = stderr if stderr is not None else node.stderr

        # 3. Execute
        cmd_name = current_args[0]
        if cmd_name in self.commands.keys():
          func = self.commands[cmd_name]
          this_job = Job(func, args=current_args[1:], stdin=actual_stdin, stdout=actual_stdout, stderr=actual_stderr)
        
        else:
          # External command (subprocess)
          try:
            # Unwrap JobIO objects to raw FDs for subprocess
            p_stdin = actual_stdin._r_fd if isinstance(actual_stdin, JobIO) else actual_stdin
            p_stdout = actual_stdout._w_fd if isinstance(actual_stdout, JobIO) else actual_stdout
            p_stderr = actual_stderr._w_fd if isinstance(actual_stderr, JobIO) else actual_stderr

            this_job = subprocess.Popen(current_args, stdin=p_stdin, stdout=p_stdout, stderr=p_stderr)
            
            # NOTE: For subprocess, we MUST close the pipe ends in the parent 
            # so the child is the only one holding the write end. 
            # However, since we are mixing JobIO with subprocess, this gets tricky.
            # Ideally, wrap subprocess in your Job class or handle closing specifically here.
            
            # If we passed a JobIO write-fd to Popen, we can close our copy now if we don't need it.
            if isinstance(actual_stdout, JobIO) and actual_stdout._w_fd:
                 os.close(actual_stdout._w_fd)
                 actual_stdout._w_fd = None # Mark as closed in wrapper

          except FileNotFoundError:
            print(f"Command not found: {cmd_name}")
            return None

        # REMOVED: The block that forcefully closed node.stdout._w_fd
        # This prevents the "Threading" bug and the "NoneType" race condition.

      elif isinstance(node, SubshellNode):
        this_job = self.execute_ast(node.children)

    return this_job

  def add_command(self, command):
    """ Explicitly adds a command to the shell.

    Args:
      command: The Shell.Command object to add to the shell.

    Returns:
      None
    """
    self.commands[command.name] = command

  def expand_variables(self, user_input):
    """ Expands variables from user input.

    Args:
      user_input: The list of arguments from the user input.

    Returns:
      An updated list of user_input with variables expanded.
    """
    # Pattern: Finds $VAR_NAME or $?, $!, $$
    pattern = r"\$([a-zA-Z_][a-zA-Z0-9_]*|[?!$])"

    def replace_match(match):
      var_name = match.group(1)
      return self.variables.get(var_name, "") # Return value or empty string

    output_list = []
    for list_item in user_input:
      if isinstance(list_item, CommandSubstitutionNode):
        output_list.append(list_item)
      else:
        output_list.append(re.sub(pattern, replace_match, list_item))

    return output_list
    # return [re.sub(pattern, replace_match, user_string) for user_string in user_input]


  def expand_command_substitutions(self, args: list) -> list:
    """
    Scans arguments for $(cmd) syntax, executes them, and substitutes the output.
    """
    new_args = []

    for arg in args:
      if isinstance(arg, CommandSubstitutionNode):
        output_buffer = JobIO()
        arg.inner_ast[-1].stdout = output_buffer

        proc = self.execute_ast(arg.inner_ast)
        
        # 1. Wait for process/thread to finish
        stdout, stderr = proc.communicate()

        # 2. Determine where the output is
        # If 'stdout' has data, the Job class already drained the pipe for us.
        # If 'stdout' is None, it was a Popen object using FDs, so data is still in output_buffer.
        if stdout:
            output_str = stdout
        else:
            output_str = output_buffer.read()

        # 3. Clean up and Append
        if isinstance(output_str, bytes):
            output_str = output_str.decode('utf-8')

        new_args.append(output_str.strip())

      else:
        new_args.append(arg)

    return new_args

  def reassemble_substitutions(self, tokens: list) -> list:
    """
    Merges tokens split by spaces back together if they are inside $( ... ).
    Ex: ['echo', '$(', date', '+%s', ')'] -> ['echo', '$(date +%s)']
    """
    new_tokens = []
    buffer = []
    depth = 0

    for token in tokens:
      # Check for opening $(
      if '$(' in token:
        depth += token.count('$(')

        # Check for closing )
        if ')' in token and depth > 0:
          depth -= token.count(')')

      if depth > 0:
        buffer.append(token)
      elif buffer:
        # We just finished a block
        buffer.append(token)
        new_tokens.append(" ".join(buffer))
        buffer = []
      else:
        new_tokens.append(token)

    if buffer: # Mismatched parens, just dump the buffer
      new_tokens.extend(buffer)

    return new_tokens


  def handle_variable_assignment(self, command):
    """ Handles assignment of variables within the shell.

    Args:
      command: The input command from the user.

    Returns:
      True: If command was a valid variable assignment.
      False: If command wasn't a valid variable assignment.
    """
    # Check for syntax: VAR=value
    if "=" in command:
      parts = command.split("=", 1)
      key = parts[0].strip()
      # Ensure key is a valid variable name (no spaces, starts with letter)
      if " " not in key and key.isidentifier():
        self.variables[key] = parts[1].strip()
        return True # Signal that we handled this internally
    return False


  def __call__(self): # Just a wrapper for calling shell.open() by calling shell()
    """ Same as shell.open()
    """
    self.open()

  def open(self):
    """ Enters the interactive shell session with the currently configured command setup.

    Args:
      None

    Returns:
      None
    """
    try:
      while True:
        user_input = shellparser.get_input(self.prompt)
        shellparser.add_history(user_input)
        tokens = shellparser.parse_args(user_input)

        if not tokens: continue


        tokens = self.reassemble_substitutions(tokens)

        # 1. Build AST
        ast_root = build_ast(tokens)
        ast_root = link_pipes(ast_root)



        # 2. Execute
        final_process = self.execute_ast(ast_root)

        # 3. Wait for the final command in the pipeline to finish
        if final_process:
          stdout, stderr = final_process.communicate()
          if stdout is not None: sys.stdout.write(stdout)
          if stderr is not None: sys.stderr.write(stderr)
          self.variables['?'] = str(final_process.returncode)

    except KeyboardInterrupt:
      sys.stdin = sys.__stdin__
      sys.stdout = sys.__stdout__
      sys.stderr = sys.__stderr__
      print("\n")


if __name__ == "__main__":
  myshell = Shell()
  myshell.open()
