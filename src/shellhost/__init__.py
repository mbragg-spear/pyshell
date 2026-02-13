# shellhost __init__.py
import platform
import inspect
import shellparser
import os
import re

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
  def __init__(self, name='shell', prompt='shell> '):
    """ Initializes a new Shell object.

    Args:
      name: The string name for your shell. Default: shell (UNUSED)
      prompt: The prompt that will be displayed on the command line. Default: 'shell> '

    Returns:
      A new Shell object.
    """
    self.name = name
    self.commands = {
        'exit': self._exit,
        'help': self._help,
        'env': self._env,
        'echo': self._echo
    }
    self.error_message = None
    self.name = name
    self.prompt = prompt

    ### Stateful environment tracking
    self.variables = os.environ.copy()
    self.variables['?'] = '0'                # Return values from commands
    self.variables['!'] = str(os.getpid())   # Current PID


  def _exit(self, *user_input):
    """ Safely calls the python exit() function from within the shell.

    Args:
      None

    Returns:
      None
    """
    exit()

  def _echo(self, user_input):
    """ Prints whatever the user input is.

    Args:
      user_input: The input provided by the user on the command line.

    Returns:
      None
    """
    print(' '.join(user_input))



  def _env(self, *args):
    """ Prints all currently stored environment variables.

    Args:
      None (*args is in the signature to handle empty argument lists).

    Returns:
      None
    """
    for K,V in self.variables.items():
      print(f"{K} = {V}")


  def _help(self, user_input=None, *args):
    """ Prints help messages for commands.

    Args:
      user_input: The name of the command specifically to view a help message for.

    Returns:
      "Command not found.": If the user requested a specific command that could not be located.
      None: Otherwise
    """


    if user_input is not None and len(user_input) > 0:
      user_func = self.commands.get(user_input[0])
      if user_func is None:
        print(f"Error - help: Command {user_input[0]} not found.")
        return 'Command not found.'

      if type(user_func) == Command:
        help(user_func.func)
      else:
        help(user_func)

    else:
      for command_name, command_function in self.commands.items():
        print(f"Command: {command_name}")

    print("\n")

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

    return [re.sub(pattern, replace_match, user_string) for user_string in user_input]

  def expand_subshells(self, user_input):
    """ Expands subshell operators from the user input.

    Args:
      user_input: The list of arguments from the user input.

    Returns:
      An updated list of user_input with subshells expanded.
    """

    # Pattern: Finds $(*)
    return user_input


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
        self.error_message = None
        user_input = shellparser.get_input(self.prompt) # Get the user input string.

        user_input = shellparser.parse_args(user_input) # Split the input string to individual arguments.

        if user_input is None:
          continue # User pressed enter with no input.

        ### [1]: Expand variables
        user_input = self.expand_variables(user_input)


        ### [2]: Expand subshells
        user_input = self.expand_subshells(user_input)

        if self.handle_variable_assignment(user_input[0]): # Returns True if successful
          continue # User assigned a variable, no execution needed.


        user_command = self.commands.get(user_input[0]) # Assume the actual command is the first item.
        try:

          if user_command is None:
            print(f"Error: Command {user_input[0]} not found.\n")
            self.variables['?'] = 'Error: Command not found.'

          else:
            self.variables['?'] = user_command(user_input[1:])

          shellparser.add_history(' '.join(user_input))

        except (Command.ParsingError, Command.ArgumentError) as e:
          print(e)


    except Exception as e:
      self.error_message = str(e)
      print(f"Error: {e}")

    finally:
      if self.error_message is not None:
        print(f"Error: {self.error_message}")


class Command:
  def __init__(self, name, func):
    """ Initializes a Shell Command object.

    Args:
      self: The Command object.
      command_name: The name of the command that will be called on the command line.
      func: The function that the command will execute.

    Returns:
      A new Command object.
    """

    self.name = name
    self.func = func
    self.positional_arguments = {}
    self.optional_arguments = {}

  class ParsingError(Exception):
    def __init__(self, message=''):
      final_message = "Error while parsing command line: " + message if len(message) > 0 else "Error while parsing command line arguments."
      super().__init__(final_message)

  class ArgumentError(Exception):
    def __init__(self, message=''):
      final_message = "Error while executing command: " + message if len(message) > 0 else "Error while executing command."
      super().__init__(final_message)


  def get_args(self) -> dict:
    """ Returns a dictionary containing all of the arguments for the current command.

    Args:
      self: The Command object.

    Returns:
      A dictionary of the currently establish args separated by 'positional' and 'optional' keys.

    """
    return {'positional':self.positional_arguments, 'optional':self.optional_arguments}


  def __call__(self, arg_list):
    """ Overloaded __call__ so that Command objects can be used like functions.

    Args:
      self: The Command object.
      arg_list: A list of individual arguments parsed by the C library interface.

    Returns:
      Whatever the function for this Command returns.
    """
    p_args, o_args = self.parse(arg_list)
    try:
      return self.func(*p_args, **o_args)
    except Exception as e:
      raise Command.ArgumentError(str(e))

  @classmethod
  def decorator(self, func):
    """ Function decorator for creating a Shell command from a python function.

    Args:
      self: The Command class definition.
      func: The function to be decorated.

    Returns:
      A Shell Command object.
    """

    this_command = self(func.__name__, func) # Create new Command object.

    sig = inspect.signature(func) # Get signature of decorated function.


    for name, param in sig.parameters.items():
      optional_arg = False if param.default == inspect._empty else True # Optional args are determined from the function signature by the presence of a default value.
      boolean_arg = True if type(param.default) == bool else False
      arg_nargs = '*' if param.kind == param.VAR_POSITIONAL else None
      arg_dtype = param.annotation if param.annotation != inspect._empty else None
      arg_default = param.default if param.default != inspect._empty else None
      arg_name = param.name

      if optional_arg: # QoL configs for optional args
        arg_name = '--'+param.name

        # Check if there is already a shorthand of this option.
        if this_command.optional_arguments.get('-'+param.name[0]) is None:
          arg_name = '-'+param.name[0]+'|'+arg_name # If there is not already a shorthand for this option, then add it.


      this_command.add_arg(arg_name, nargs=arg_nargs, dtype=arg_dtype, default=arg_default, sig_name = param.name, is_bool=boolean_arg)

    return this_command


  def add_arg(self, name: str, nargs = None, dtype = None, default = None, sig_name = None, is_bool = False) -> None:
    """ Adds an argument to the dictionary of parse-able arguments.

    Args:
      self: The Command object
      name: The name/definition of the argument.
      nargs: The number of values accepted by this argument. Only to be used with optional arguments.
      dtype: The accepted type of value for this argument.

    Returns:
      None

    Raises:
      TypeError: If name is not a string.
      ValueError: If nargs > 0 and is_bool is True.
      ValueError: If nargs == 0 and is_bool is False.
    """

    if type(name) != str:
      raise TypeError(f"Expected type str for name, got {type(name)}.")

    arg_type = 'Positional'                   # ┐
    arg_dict = self.positional_arguments      # ┤
    arg_name_original = name                  # ┤ Default argument definitions.
    arg_name_formatted = name                 # ┤
    name_parts_original = [arg_name_original] # ┘

    if dtype == bool:
      is_bool = True


    if name[0] == '-': # Leading hyphens determine if the argument is optional.
      arg_type = 'Optional'
      arg_dict = self.optional_arguments

      if is_bool and (nargs is not None and nargs != 0): # Declared argument as a boolean but specified non-zero number of values.
        raise ValueError(f"Cannot accept values for a boolean argument.")

      elif not is_bool and nargs == 0: # Declared argument as non-boolean but specified 0 arguments.
        raise ValueError(f"Cannot add non-boolean optional argument with nargs {nargs}.")


      elif is_bool:
        nargs = 0

      elif nargs is None: # Set default number of values accepted to 1 on non-boolean optional arguments.
        nargs = 1

      name_parts_original = name.split('|')  # Split name on pipe character (logical OR operator) and iterate over the pieces.
      name_parts_formatted = []
      for i in range(len(name_parts_original)):
        name_parts_formatted.append(name_parts_original[i].lstrip('-').replace('-','_')) # Format the arg names to remove leading hyphens and replace other hyphens with underscores.


      name_lengths = [len(P) for P in name_parts_formatted]         # ┐
      longest_name_index = name_lengths.index(max(name_lengths))    # ┤ Use the argument with the longest name as the default formatted name.
      arg_name_formatted = name_parts_formatted[longest_name_index] # ┘ NOTE: Maybe switch this to use the first instance of double hyphen?


    this_arg = {
      "type":arg_type,
      "original_name":arg_name_original,
      "formatted_name":arg_name_formatted,
      "dtype":dtype,
      "nargs":nargs,
      "default":default,
      "is_bool":is_bool,
      "sig_name":sig_name,
    }

    for _name in name_parts_original:
      arg_dict[_name] = this_arg




  def parse(self, cli_args: list) -> tuple:
    """ Parses a given string based on currently defined arguments.

    Args:
      self: The Command object
      args: The arguments that were passed to the shell command - NOT including the command name.

    Returns:
      The input string fit into a ParsedArgs object with attributes matching the arguments identified.

    Raises:
      TypeError: If input_str is not a string.
    """


    if type(cli_args) != list:
      raise TypeError(f"Expected type list for cli_args, got {type(cli_args)}.")

    positional_args = []
    optional_args = {}

    if len(cli_args) == 0:
      return positional_args, optional_args

    try:
      for i, arg in enumerate(self.positional_arguments.values()): # Iterate over positional arguments first.
        if arg.get('nargs') == '*':       # ┬ If any positional arg has nargs as * then it
          positional_args = cli_args[:]   # ├ represents '*args' in the function signature, so
          cli_args = []                   # └ give it all remaining args.
          break

        else:
           positional_args.append(cli_args.pop(0))

    except Exception as e:
      raise Command.ParsingError("Not enough arguments.")

    if len(cli_args) > 0: # If we have any args left, they're optional args.

      i = 0 # Iterator integer
      while i < len(cli_args):
        current_option = self.optional_arguments.get(cli_args[i]) # Assume the value at cli_args[i] is a new option.

        if current_option is None: # Handle the case where we don't have an entry for the current option.
          raise Command.ParsingError(f"Got unexpected optional argument: {cli_args[i]}")

        if current_option.get('is_bool'): # Handle boolean options
          optional_args[current_option.get('formatted_name')] = True
          i += 1
          continue

        if current_option.get('nargs') == "*": # Set * nargs to be the remaining length of cli_args
          current_option['nargs'] = len(cli_args) - i


        optional_args[current_option.get('formatted_name')] = cli_args[(i+1):(i+1+current_option.get('nargs'))] # ┬ Set the values of the current_option to be
                                                                                                                # ├ a range from cli_args from the current index + 1
                                                                                                                # └ to the current index + 1 + nargs for current option.

        i += current_option.get('nargs') + 1 # Increment i by the number of arguments we've just captured.

    return positional_args, optional_args


### Mainguard to open a basic shell if called directly.
if __name__ == "__main__":

  myshell = Shell()
  myshell.open()


