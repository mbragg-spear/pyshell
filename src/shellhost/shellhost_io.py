# shellhost_io.py

import shellparser
import subprocess
import threading
import fcntl
import sys
import os

class JobIO:
  def __init__(self):
    self._r_fd, self._w_fd = os.pipe()

#    fl = fcntl.fcntl(self._r_fd, fcntl.F_GETFL)
#    fcntl.fcntl(self._r_fd, fcntl.F_SETFL, fl | os.O_NONBLOCK)

    self.closed = False

  def write(self, data):
    """Write bytes to the pipe."""
    if isinstance(data, str):
      data = data.encode('utf-8')  # Pipes deal in bytes
    os.write(self._w_fd, data)

  def read(self):
    """Read bytes from the pipe."""
    try:
      # Read up to 4KB (or whatever buffer size you want)
      return os.read(self._r_fd, 4096).decode('utf-8')
    except BlockingIOError:
      return ""  # No data currently available
    except OSError:
      return ""

  def fileno(self):
    return self._r_fd

  def _close(self, fd):
    if self._r_fd == fd:
      try: os.close(fd)
      except OSError: pass
      self._r_fd = None
    elif self._w_fd == fd:
      try: os.close(fd)
      except OSError: pass
      self._w_fd = None

  def close(self):
    """Clean up FDs."""
    # Close write end first to signal EOF to readers
    if self._w_fd:
      try: os.close(self._w_fd)
      except OSError: pass
      self._w_fd = None

    if self._r_fd:
      try: os.close(self._r_fd)
      except OSError: pass
      self._r_fd = None

    self.closed = True

  def __del__(self):
    self.close()

  def __repr__(self):
    return f"<shellhost.JobIO _r_fd={self._r_fd} _w_fd={self._w_fd} closed={self.closed}>"

class Job:
  """
  Acts like subprocess.Popen but runs a local python function in a thread.
  """
  def __init__(self, target_func, args=(), kwargs=None, stdin=None, stdout=None, stderr=None):
    self.args = args
    self.kwargs = kwargs or {}

    # --- 1. Handle STDIN ---
    if stdin == subprocess.PIPE:
      # User wants to write to the process via stdin
      self.stdin = JobIO()
    elif stdin is None:
      # Inherit from parent (or None implies no input for a func)
      self.stdin = None
    else:
      # User passed a file object
      self.stdin = stdin

    # --- 2. Handle STDOUT ---
    # Internal variable to hold where the FUNCTION writes to
    self._stdout_target = None

    if stdout == subprocess.PIPE:
      # User wants to read the output
      self.stdout = JobIO()
      self._stdout_target = self.stdout
    elif stdout is None:
      # Default: Inherit sys.stdout (print to console)
      self.stdout = None
      self._stdout_target = sys.stdout
    else:
      # User passed a file object
      self.stdout = stdout # Popen sets self.stdout to None if a file is passed
      self._stdout_target = stdout

    # --- 3. Handle STDERR ---
    self._stderr_target = None

    if stderr == subprocess.PIPE:
      self.stderr = JobIO()
      self._stderr_target = self.stderr
    elif stderr == subprocess.STDOUT:
      # Redirect stderr to stdout
      self.stderr = self.stdout # Popen sets self.stderr to None or the stdout handle
      self._stderr_target = self._stdout_target
    elif stderr is None:
      self.stderr = None
      self._stderr_target = sys.stderr
    else:
      self.stderr = stderr
      self._stderr_target = stderr


    self.io = IOManager(
      stdin_stream = self.stdin,
      stdout_sink = self.stdout,
      stderr_sink = self.stderr
    )



    # 2. Popen Attributes
    self.returncode = None
    self.pid = None # Threads don't have PIDs, but we can use ident later
    self._target_func = target_func

    # 3. Execution (The "Process")
    # We wrap the function to capture return values/errors into returncode
    self._thread = threading.Thread(target=self._run_wrapper)
    self._thread.start()
    self.pid = self._thread.ident # Mimic PID with Thread ID

  def _run_wrapper(self):
    """Internal wrapper to capture exit status."""
    try:
      with self.io:
        try:
          stdin_data = self.args
          if self.stdin is not None and self.stdin is not sys.__stdin__:
            stdin_data = self.stdin.read()
            stdin_data = shellparser.parse_args(stdin_data)

          self._target_func(*stdin_data)
          self.returncode = 0
        except Exception as e:
          # Write exception to stderr buffer mimicking a crash
          self.stderr.write(f"-- {self._target_func.name} -- {stdin_data} | Error: {str(e)} \n")
          self.returncode = 1
    finally:
      if self.stdout and self.stdout is not sys.__stdout__: self.stdout._close(self.stdout._w_fd)
      if self.stderr and self.stderr is not sys.__stderr__: self.stderr._close(self.stderr._w_fd)


  def poll(self):
    """Check if thread is alive. Sets returncode if done."""
    if not self._thread.is_alive() and self.returncode is None:
      # Thread died silently?
      self.returncode = 0
      return self.returncode

  def wait(self, timeout=None):
    """Block until the thread finishes."""
    self._thread.join(timeout=timeout)
    if self._thread.is_alive():
      # Timed out      raise Exception("TimeoutExpired") # In real Popen this raises subprocess.TimeoutExpired
      return self.returncode

  def communicate(self, input_data=None, timeout=None):
    """
    Send data to stdin. Read data from stdout and stderr.
    """
    if input_data and self.stdin:
      self.stdin.write(input_data)
      self.stdin.close() # Signal EOF to the function

    self.wait(timeout)
    stdout_data = ""
    stderr_data = ""

    # In a real Popen, this returns bytes/strings.
    try:
      stdout_data = self.stdout.read()
      stderr_data = self.stderr.read()
    except Exception as e:
      pass
      # print(f"[ERROR] Unable to return data from Job.communicate: {str(e)}", file=sys.__stderr__)

    return stdout_data, stderr_data

  def send_signal(self, signal):
    """Threads cannot handle signals easily. Pass."""
    pass

  def terminate(self):
    """Impossible to force-terminate a thread safely."""
    # Best practice: set a flag here that your function checks periodically
    pass

  def kill(self):
    self.terminate()

  def __enter__(self):
    return self

  def __exit__(self, exc_type, value, traceback):
    if self.stdout: self.stdout.close()
    if self.stderr: self.stderr.close()
    if self.stdin: self.stdin.close()
    self.wait()


class IOManager:
  def __init__(self, stdin_stream=None, stdout_sink=None, stderr_sink=None):
    self.stdin_stream = stdin_stream if stdin_stream is not None else JobIO()
    self.stdout_sink = stdout_sink if stdout_sink is not None else JobIO()
    self.stderr_sink = stderr_sink if stderr_sink is not None else JobIO()


  def get_stdout(self):
    return self.stdout_sink.get_value()

  def get_stderr(self):
    return self.stderr_sink.get_value()

  def write_stdin(self, str_input):
    if str_input is None: return
    self.stdin_stream.write(str_input)

  def __enter__(self):
    self.original_stdin = sys.stdin
    self.original_stdout = sys.stdout
    self.original_stderr = sys.stderr

    sys.stdin = self.stdin_stream
    sys.stdout = self.stdout_sink
    sys.stderr = self.stderr_sink

    return self

  def __exit__(self, exc_type, exc_value, traceback):
    sys.stdin = self.original_stdin
    sys.stdout = self.original_stdout
    sys.stderr = self.original_stderr


    return False
