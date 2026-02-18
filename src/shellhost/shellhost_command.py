# shellhost __init__.py
import inspect
import shell_core
import sys

class Command:
  def __init__(self, name, func, register=True):
    """ Initializes a Shell Command object.

    Args:
      self: The Command object.
      command_name: The name of the command that will be called on the command line.
      func: The function that the command will execute.

    Returns:
      A new Command object.
    """

    self.name = name
    self.__name__ = self.name
    self.func = func
    self.positional_arguments = {}
    self.optional_arguments = {}

    if register: shell_core.register(self.name, self)

  class ParsingError(Exception):
    def __init__(self, message=''):
      final_message = "Error while parsing command line: " + message if len(message) > 0 else "Error while parsing command line arguments."
      super().__init__(final_message)

  class ArgumentError(Exception):
    def __init__(self, message=''):
      final_message = "Error while executing command: " + message if len(message) > 0 else "Error while executing command."
      super().__init__(final_message)

  def set_name(self, new_name: str) -> None:
    self.name = new_name
    self.__name__ = new_name


  def get_args(self) -> dict:
    """ Returns a dictionary containing all of the arguments for the current command.

    Args:
      self: The Command object.

    Returns:
      A dictionary of the currently establish args separated by 'positional' and 'optional' keys.

    """
    return {'positional':self.positional_arguments, 'optional':self.optional_arguments}


  def __call__(self, *args, **kwargs):
    """ Overloaded __call__ so that Command objects can be used like functions.

    Args:
      self: The Command object.
      arg_list: A list of individual arguments parsed by the C library interface.

    Returns:
      Whatever the function for this Command returns.
    """
    this_command = args[0]
    args = args[1:]
    p_args, o_args = self.parse(args)
    try:
      if p_args is not None and o_args is not None: return self.func(*p_args, **o_args)
      elif p_args is not None: return self.func(*p_args)
      elif o_args is not None: return self.func(**o_args)
      else: return self.func()

    except Exception as e:
      raise Command.ArgumentError(str(e))

  @classmethod
  def command(self, func):
    """
    Function decorator for creating a Shell command from a python function,
    without setting any values for it or registering it.

    Args:
      self: The Command class definition.
      func: The function to be decorated.

    Returns:
      A Shell Command object.
    """
    this_command = self(func.__name__, func, register=False)
    return this_command


  @classmethod
  def auto_command(self, func):
    """
    Function decorator for creating a Shell command from a python function,
    while also automatically generating its argument list and registering it.

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


  def add_arg(self, name: str, optional = False, positional = True, nargs = None, dtype = None, default = None, sig_name = None, is_bool = False) -> None:
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

    arg_type = 'Positional'                     # ┐
    arg_dict = self.positional_arguments        # ┤
    arg_name_original = name                    # ┤ Default argument definitions.
    arg_name_formatted = name                   # ┤
    name_parts_original = [arg_name_original]   # ┘

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




  def parse(self, cli_args = None) -> tuple:
    """ Parses a given string based on currently defined arguments.

    Args:
      self: The Command object
      args: The arguments that were passed to the shell command - NOT including the command name.

    Returns:
      The input string fit into a ParsedArgs object with attributes matching the arguments identified.

    Raises:
      TypeError: If input_str is not a string.
    """

    if cli_args is None or len(cli_args) == 0:
      return (None, None)

    if type(cli_args) == tuple:
      cli_args = list(cli_args)

    if type(cli_args) != list:
      raise TypeError(f"Expected type list for cli_args, got {type(cli_args)}.")

    positional_args = []
    optional_args = {}

    try:
      for i, arg in enumerate(self.positional_arguments.values()): # Iterate over positional arguments first.
        if arg.get('nargs') == '*':       # ┬ If any positional arg has nargs as * then it
          positional_args = cli_args[:]   # ├ represents '*args' in the function signature, so
          cli_args = []                   # └ give it all remaining args.
          break

        else:
           arg_val = cli_args.pop(0)
           if arg.get('dtype') is not None: arg_val = arg.get('dtype')(arg_val)
           positional_args.append(arg_val)

    except Exception as e:
      raise Command.ParsingError(f"{e} | Expected {arg.get('nargs')} arguments, got {len(cli_args)}.")

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

        if current_option.get('nargs') == 1:
          optional_args[current_option.get('formatted_name')] = cli_args[i+1]


        else:

          if current_option.get('nargs') == "*": # Set * nargs to be the remaining length of cli_args
            current_option['nargs'] = len(cli_args) - i

          optional_args[current_option.get('formatted_name')] = cli_args[(i+1):(i+1+current_option.get('nargs'))] # ┬ Set the values of the current_option to be
                                                                                                                # ├ a range from cli_args from the current index + 1
                                                                                                                # └ to the current index + 1 + nargs for current option.

        i += current_option.get('nargs') + 1 # Increment i by the number of arguments we've just captured.

    else:
      optional_args = None

    return (positional_args, optional_args)
