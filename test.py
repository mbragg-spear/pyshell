# test.py

from pyshell import PyShell, Command

sh = PyShell()

@Command.decorator
def test(*args) -> int:
  """ PyShell test that prints a basic confirmation message to the screen upon execution.

  Args:
    None

  Returns:
    0

  """
  print("### PyShell Command executed successfully! ###\n")
  return 0


@Command.decorator
def pa_test(*args) -> int:
  """ PyShell test that prints all the arguments it could find back to the screen.

  Args:
    *args: (just add as many as you want)

  Returns:
    0

  """
  print("[PyShell Test with *args]")
  for i,A in enumerate(args):
    print(f"-- [arg {i}] {A}")

  print("\n")

  return 0


@Command.decorator
def oa_test(kwarg=None, boolean=False):
  """ PyShell test that prints the status of the two optional arguments within the function.

  Args:
    kwarg (-k|--kwarg): Anything you want to put in as a test. Default: None
    boolean (-b|--boolean): A boolean switch argument which takes no values. Default: False

  Returns:
    0

  """
  print("[PyShell Test with kwargs]")
  print(f"-- Keyword Argument (kwarg): {kwarg}")
  print(f"-- Bool switch included: {boolean}")
  print("\n")

  return 0

@Command.decorator
def basic_print(*user_input):
  print(user_input)

sh.add_command(test)
sh.add_command(pa_test)
sh.add_command(oa_test)
sh.add_command(basic_print)


sh.open()
