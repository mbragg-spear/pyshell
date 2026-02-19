// SUPPRESS MICROSOFT WARNINGS
// These must be defined BEFORE any includes
#define _CRT_SECURE_NO_WARNINGS    // Shuts up "strcpy is unsafe"
#define _CRT_NONSTDC_NO_DEPRECATE  // Shuts up "strdup is deprecated"
#define PY_SSIZE_T_CLEAN

#include <Python.h> // Must be included first

// WINDOWS COMPATIBILITY BLOCK
#ifdef _WIN32
  #include <windows.h>
  #include <process.h>
  #include <io.h>
  #include <fcntl.h>
  #include <conio.h> // Fixes "warning C4013: '_getch' undefined"

  // Map POSIX names to Windows CRT functions
  #define pipe(fds) _pipe(fds, 4096, _O_BINARY)
  #define close _close
  #define read _read
  #define write _write
  #define dup _dup
  #define dup2 _dup2
  #define fileno _fileno
  #define isatty _isatty
  #define unlink _unlink
  #define strdup _strdup // Fixes "warning C4996: strdup deprecated"

  #define STDIN_FILENO 0
  #define STDOUT_FILENO 1
  #define STDERR_FILENO 2

  // Windows setenv replacement
  int setenv(const char *name, const char *value, int overwrite) {
    if (!overwrite && getenv(name)) return 0;
    return _putenv_s(name, value);
  }

  // REMOVED: typedef intptr_t pid_t;
  // Python.h already defines pid_t on Windows. Redefining it causes Error C2371.

#else
  // Linux / MacOS
  #include <sys/wait.h>
  #include <unistd.h>
  #include <termios.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#define MAX_HISTORY 50
#define MAX_ARGS 64
#define MAX_CMD_LEN 1024
#define PY_SSIZE_T_CLEAN

// -- GLOBAL HISTORY STORAGE --
static char history[MAX_HISTORY][MAX_CMD_LEN];
static int history_count = 0;
static int history_view_idx = 0; // Where the user is currently looking

// A simple linked list to store Python callbacks
typedef struct PyCommand {
  char *name;
  PyObject *func;
  struct PyCommand *next;
} PyCommand;

static PyCommand *py_cmd_head = NULL;




// Cross-platform Spawner
pid_t spawn_command(char **argv, int input_fd, int output_fd) {
#ifdef _WIN32
    int orig_stdin = dup(STDIN_FILENO);
    int orig_stdout = dup(STDOUT_FILENO);

    if (input_fd != STDIN_FILENO) dup2(input_fd, STDIN_FILENO);
    if (output_fd != STDOUT_FILENO) dup2(output_fd, STDOUT_FILENO);

    pid_t pid = _spawnvp(_P_NOWAIT, argv[0], argv);

    dup2(orig_stdin, STDIN_FILENO);
    dup2(orig_stdout, STDOUT_FILENO);
    close(orig_stdin);
    close(orig_stdout);

    return pid;
#else
    pid_t pid = fork();
    if (pid == 0) {
        if (input_fd != STDIN_FILENO) dup2(input_fd, STDIN_FILENO);
        if (output_fd != STDOUT_FILENO) dup2(output_fd, STDOUT_FILENO);
        execvp(argv[0], argv);
        perror("execvp failed");
        exit(1);
    }
    return pid;
#endif
}



// --- PYTHON MODULE METHODS (Exposed to Python) ---

void register_python_command(const char *name, PyObject *func) {
  PyCommand *new_cmd = malloc(sizeof(PyCommand));
  new_cmd->name = strdup(name);
  new_cmd->func = func;
  Py_INCREF(func); // Keep function alive
  new_cmd->next = py_cmd_head;
  py_cmd_head = new_cmd;
}

PyCommand* find_python_command(const char *name) {
  PyCommand *current = py_cmd_head;
  while (current != NULL) {
    if (strcmp(current->name, name) == 0) {
      return current;
    }
    current = current->next;
  }
  return NULL;
}

//  Return a list of all registered command names
static PyObject* shell_get_registry(PyObject *self, PyObject *args) {
  PyObject *list = PyList_New(0);
  PyCommand *curr = py_cmd_head;
  while (curr) {
    PyObject *str = PyUnicode_FromString(curr->name);
    PyList_Append(list, str);
    Py_DECREF(str);
    curr = curr->next;
  }
  return list;
}

// Return the Python function object for a given name
static PyObject* shell_get_command(PyObject *self, PyObject *args) {
  const char *name;
  if (!PyArg_ParseTuple(args, "s", &name)) {
    return NULL;
  }

  PyCommand *cmd = find_python_command(name);
  if (cmd) {
    Py_INCREF(cmd->func); // Must increment refcount when returning to Python
    return cmd->func;
  }

  Py_RETURN_NONE;
}

// Python calls this: shell_core.register("my_cmd", func)
static PyObject* shell_register(PyObject *self, PyObject *args) {
  const char *name;
  PyObject *func;

  if (!PyArg_ParseTuple(args, "sO", &name, &func)) {
    return NULL;
  }

  if (!PyCallable_Check(func)) {
    PyErr_SetString(PyExc_TypeError, "Second argument must be callable");
    return NULL;
  }

  register_python_command(name, func);
  Py_RETURN_NONE;
}

// --- C HELPER FUNCTIONS ---


// WINDOWS IMPLEMENTATION FOR KEYBOARD INPUT
#ifdef _WIN32
  // Windows doesn't need "enableRawMode" the same way,
  // but we might need to disable line buffering.
  void enableRawMode() {
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(hStdin, &mode);
    // Disable Line Input and Echo
    SetConsoleMode(hStdin, mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
  }

  void disableRawMode() {
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(hStdin, &mode);
    // Re-enable Line Input and Echo
    SetConsoleMode(hStdin, mode | (ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
  }

  // Windows has _getch() which reads a char without echo (Raw by default)
  int read_char() {
    return _getch();
  }

// LINUX / MACOS IMPLEMENTATION FOR KEYBOARD INPUT
#else
  struct termios orig_termios;

  void enableRawMode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  }

  void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
  }

  int read_char() {
    char c;
    if (read(STDIN_FILENO, &c, 1) == 1) return c;
    return -1; // Error
  }
#endif

// Helper to add an argument to the dynamic argv list
void add_arg(char ***argv, int *argc, const char *buffer) {
  *argv = realloc(*argv, sizeof(char*) * (*argc + 2)); // Resize array
  (*argv)[*argc] = strdup(buffer); // Copy string
  (*argc)++;
  (*argv)[*argc] = NULL; // Null terminate list
}

// Save a command line to the shell history buffer.
void add_history(const char* cmd) {
  if (strlen(cmd) == 0) return;

  // Shift everything down if full (Simple implementation)
  if (history_count == MAX_HISTORY) {
    for (int i = 0; i < MAX_HISTORY - 1; i++) {
      strcpy(history[i], history[i+1]);
    }
    history_count--;
  }

  // Add to end
  strncpy(history[history_count], cmd, MAX_CMD_LEN - 1);
  history[history_count][MAX_CMD_LEN - 1] = '\0';
  history_count++;

  // Reset view index to the NEW end (so pressing UP goes to the latest)
  history_view_idx = history_count;
}


void replace_line(char* buffer, size_t* length, size_t* cursor_idx, const char* new_text, const char* term_prompt) {
  // Fix visuals
  printf("\r");               // Move to line start
  printf("\x1b[K");           // Print ANSI code to clear the line
  printf("%s", term_prompt);  // Print prompt
  printf("%s", new_text);     // Print buffer content

  // Update Buffer Memory
  strcpy(buffer, new_text);
  *length = strlen(buffer);
  *cursor_idx = *length; // Set cursor to end of line

  fflush(stdout);
}

// --- EXECUTION ENGINE ---

int execute_python_command(const char *name, char **args, int input_fd, int output_fd) {
  PyCommand *cmd = find_python_command(name);
  if (!cmd) return -1;

  // Construct Argument Tuple
  int argc = 0;
  while(args[argc] != NULL) argc++;
  PyObject *pArgs = PyTuple_New(argc);
  for (int i = 0; i < argc; i++) {
    PyTuple_SetItem(pArgs, i, PyUnicode_FromString(args[i]));
  }

  // Import sys
  PyObject *sys = PyImport_ImportModule("sys");
  PyObject *py_in = NULL;
  PyObject *py_out = NULL;

  // Redirect STDIN (if needed)
  if (input_fd != STDIN_FILENO) {
    // closefd=0 means: Python will NOT close input_fd when py_in is destroyed
    py_in = PyFile_FromFd(input_fd, "<stdin>", "r", -1, NULL, NULL, NULL, 0);
    PyObject_SetAttrString(sys, "stdin", py_in);
  }

  // Redirect STDOUT (if needed)
  if (output_fd != STDOUT_FILENO) {
    // closefd=0 means: Python will NOT close output_fd when py_out is destroyed
    py_out = PyFile_FromFd(output_fd, "<stdout>", "w", -1, NULL, NULL, NULL, 0);
    PyObject_SetAttrString(sys, "stdout", py_out);
  }

  // CALL THE FUNCTION
  PyObject *result = PyObject_CallObject(cmd->func, pArgs);

  // FLUSH STDOUT
  // Even though we manage the FD, Python has its own buffer we must empty.
  if (py_out) {
    PyObject_CallMethod(py_out, "flush", NULL);
  }

  // RESTORE STREAMS & CLEANUP
  // Reset sys.stdout to standard output so we don't hold a ref to the pipe
  PyObject_SetAttrString(sys, "stdin", PySys_GetObject("__stdin__"));
  PyObject_SetAttrString(sys, "stdout", PySys_GetObject("__stdout__"));

  // Cleanup our wrapper objects
  if (py_in) Py_DECREF(py_in);
  if (py_out) Py_DECREF(py_out);
  Py_DECREF(pArgs);
  Py_DECREF(sys);

  // Handle Errors
  if (result == NULL) {
    PyErr_Print(); // Print traceback if Python crashed
    return 1;
  }
  Py_DECREF(result);

  return 0;
}

int tokenize_command(char *input_str, char ***argv_ptr) {
  // Initialize output
  *argv_ptr = NULL;
  int argc = 0;

  const size_t input_size = strlen(input_str);
  size_t buff_position = 0;
  int subshell_depth = 0;
  char arg_buffer[MAX_CMD_LEN];
  char current_char;

  // Boolean values for tracking state.
  bool single_quote = false;
  bool double_quote = false;
  bool char_escaped = false;

  for (size_t i = 0; i < input_size; i++) {
    current_char = input_str[i];

    // [#0] Buffer Overflow Check
    if (buff_position >= MAX_CMD_LEN - 1) {
      fprintf(stderr, "Error: Argument exceeds maximum buffer size.\n");
      return -1;
    }

    // [#1] Escaped Character
    if (char_escaped) {
      arg_buffer[buff_position++] = current_char;
      char_escaped = false;
    }

    // [#2] Unquoted Operators (|, <, >, (, ))
    // Note: We only split on these if we aren't in a subshell or quotes
    else if (!single_quote && !double_quote && !char_escaped &&
         (current_char == '|' || current_char == '<' ||
          current_char == '>' || current_char == '(' || current_char == ')')) {

      // A. Substitution Start "$("
      if (current_char == '(' && buff_position > 0 && arg_buffer[buff_position-1] == '$') {
        subshell_depth++;
        arg_buffer[buff_position++] = current_char;
        continue;
      }

      // B. Substitution End ")"
      if (current_char == ')' && subshell_depth > 0) {
        subshell_depth--;
        arg_buffer[buff_position++] = current_char;
        continue;
      }

      // C. Inside Substitution -> Literal
      if (subshell_depth > 0) {
        arg_buffer[buff_position++] = current_char;
        continue;
      }

      // --- Delimiter Logic ---

      // I. Flush current word if exists
      if (buff_position > 0) {
        arg_buffer[buff_position] = '\0';
        add_arg(argv_ptr, &argc, arg_buffer);
        buff_position = 0;
      }

      // II. Handle Operators as separate args
      // Check for ">>"
      if (current_char == '>' && input_str[i+1] == '>') {
        add_arg(argv_ptr, &argc, ">>");
        i++; // Skip next char
      } else {
        char op_str[2] = {current_char, '\0'};
        add_arg(argv_ptr, &argc, op_str);
      }
    }

    // [#3] Whitespace
    else if (current_char == ' ' || current_char == '\n') {
      if (single_quote || double_quote || subshell_depth > 0) {
        arg_buffer[buff_position++] = current_char;
      } else {
        if (buff_position > 0) {
          arg_buffer[buff_position] = '\0';
          add_arg(argv_ptr, &argc, arg_buffer);
          buff_position = 0;
        }
      }
    }

    // [#4] Quotes
    // Note: We generally DO NOT want to include the quote characters themselves
    // in the final arg, unless you want the python script to see them.
    else if (current_char == '\'' && !double_quote) {
      single_quote = !single_quote;
    }
    else if (current_char == '\"' && !single_quote) {
      double_quote = !double_quote;
    }

    // [#5] Backslash Start
    else if (current_char == '\\' && !double_quote && !single_quote) {
      char_escaped = true;
    }

    // [Final] Regular Char
    else {
      arg_buffer[buff_position++] = current_char;
    }
  }

  // [CLEAN UP] Flush remaining buffer
  if (buff_position > 0) {
    arg_buffer[buff_position] = '\0';
    add_arg(argv_ptr, &argc, arg_buffer);
  }

  return argc;
}


// The main execution logic handling Pipes and Forking
void execute_pipeline(char *input, int default_in, int default_out) {
  char *commands[16];
  int cmd_count = 0;

  // Tokenize by pipe symbol
  char *token = strtok(input, "|");
  while(token != NULL && cmd_count < 16) {
    commands[cmd_count++] = token;
    token = strtok(NULL, "|");
  }

  int i;
  int prev_fd = STDIN_FILENO; // Read end of the previous pipe
  int pipe_fds[2];

  // Array to keep track of child PIDs so we can wait for them ALL at the end
  pid_t pids[16];
  int pid_count = 0;

  for (i = 0; i < cmd_count; i++) {

    char **argv = NULL; // Important: Initialize to NULL
    int argc = tokenize_command(commands[i], &argv);

    if (argc > 0 && argv[0] != NULL) {

      // Prepare Pipe for Next Command
      int input_fd = prev_fd;
      int output_fd = default_out;

      if (i < cmd_count - 1) {
        if (pipe(pipe_fds) == -1) { // Create pipe and make sure it worked.
          perror("pipe failed");
          exit(1);
        }
        output_fd = pipe_fds[1]; // Write to this pipe
      }

      // Determine Command Type
      PyCommand *py_cmd = find_python_command(argv[0]);

      if (py_cmd) {
        // Python commands run in the PARENT process in this architecture.
        // This blocks the parent, so we can't truly parallelize Python-to-Python pipes
        // without threading, but we MUST close FDs correctly to avoid hanging.

        execute_python_command(argv[0], argv, input_fd, output_fd);

        // Python is done, close the ends we used
        if (input_fd != default_in) close(input_fd);
        if (output_fd != default_out) close(output_fd);

        // Mark PID as 0 (skipped) since we ran it inline
        pids[pid_count++] = 0;

      } else {

        // We pass the pipe FDs. The helper handles the dup2/close logic.
        pid_t pid = spawn_command(argv, input_fd, output_fd);

        if (pid > 0) {
          pids[pid_count++] = pid;
        }

        // Parent cleanup: Close the pipe ends we handed off
        if (input_fd != STDIN_FILENO) close(input_fd);
        if (output_fd != STDOUT_FILENO) close(output_fd);
      }

      // Set up for next iteration
      if (i < cmd_count - 1) {
        prev_fd = pipe_fds[0]; // Next command reads from the read-end
      }

      // Wait for ALL children
      for (int j = 0; j < pid_count; j++) {
        if (pids[j] > 0) {
#ifdef _WIN32
  // Windows wait
  int status;
  _cwait(&status, pids[j], 0);
#else
  // POSIX wait
  waitpid(pids[j], NULL, 0);
#endif
        }
      }
    }
  }
}

// Exported function callable from Python
char* get_input(const char* prompt) {
  printf("%s", prompt);
  fflush(stdout);

  enableRawMode(); // Turn off buffering/echo

  // Allocate a buffer for the user's input
  size_t bufsize = 1024;
  size_t length = 0;
  size_t cursor_idx = 0;
  char* buffer = calloc(bufsize, sizeof(char));

  int c;
  while (1) {
    c = read_char();
    if (c == -1) { break; }

    // ---------------------------------------------------------
    // WINDOWS ARROW KEY LOGIC
    // ---------------------------------------------------------
    #ifdef _WIN32
      // --- 1. HANDLE SPECIAL KEYS (Arrows, F-Keys, etc.) ---
      // Windows sends 0 or 0xE0 (224) first for special keys
      if (c == 0 || c == 0xE0) {
        // Read the SECOND code (the actual scan code)
        int special = read_char();

        // Handle the logic, but DO NOT print 'special' or 'c'
        switch (special) {
          case 72: // UP ARROW
            if (history_view_idx > 0) {
              history_view_idx--; // Move backwards
              replace_line(buffer, &length, &cursor_idx, history[history_view_idx], prompt);
            }
            break;

          case 80: // DOWN ARROW
            if (history_view_idx < history_count) {
              history_view_idx++; // Move fowards

              if (history_view_idx == history_count) {
                // We moved past the last history item, go to empty line.
                replace_line(buffer, &length, &cursor_idx, "", prompt);
              }
              else {
                // Show next history item
                replace_line(buffer, &length, &cursor_idx, history[history_view_idx], prompt);
              }
            }
            break;

          case 75: // LEFT ARROW (K)
            if (cursor_idx > 0) {
              cursor_idx--;
              printf("\b"); // Visual backspace
              fflush(stdout);
            }
            break;
          case 77: // RIGHT ARROW (M)
            if (cursor_idx < length) {
              cursor_idx++;
              printf("%c", buffer[cursor_idx-1]);
              fflush(stdout);
              // Note: Windows console might need specific API calls for non-destructive move,
              // but printing the existing char is a cheap hack that works.
            }
            break;
          }
          // Skip the rest of the loop
          // Do not let 'c' (0xE0) or 'special' (75) get added to buffer.
          continue;
      }

      // --- 2. HANDLE ENTER ---
      // Windows sends \r (13) for Enter
      if (c == '\r' && buffer[length] != '\\') {
        buffer[length] = '\0';
        printf("\n"); // Visual newline
        break;        // Stop reading
      }

      // --- 3. HANDLE BACKSPACE ---
      if (c == 8) { // Windows Backspace is 8
        if (cursor_idx > 0) {
          // 1. Shift memory left
          memmove(&buffer[cursor_idx - 1], &buffer[cursor_idx], length - cursor_idx);

          // 2. Update stats
          cursor_idx--;
          length--;
          buffer[length] = '\0'; // Null terminate

          // 3. Visual Update
          printf("\b"); // Move back
          printf("%s", &buffer[cursor_idx]); // Print tail
          printf(" "); // Erase ghost char

          // Move cursor back to correct spot
          int steps = length - cursor_idx + 1;
          for(int i=0; i<steps; i++) printf("\b");
          fflush(stdout);
        }
        continue;
      }

      if (c >= 32 && c <= 126) {
        if (cursor_idx < length) {
        // INSERT MODE LOGIC

        // 1. Shift Right (Include +1 for Null Terminator!)
        memmove(&buffer[cursor_idx + 1], &buffer[cursor_idx], length - cursor_idx + 1);

        // 2. Insert Char
        buffer[cursor_idx] = c;
        length++;
        buffer[length] = '\0';

        // 3. Print New Char
        printf("%c", c);

        // 4. Print the shifted tail
        printf("%s", &buffer[cursor_idx + 1]);

        // 5. Move Cursor Back
        int steps = length - (cursor_idx + 1);
        for(int i=0; i<steps; i++) printf("\b");
        fflush(stdout);

        cursor_idx++;
      } else {
        // APPEND MODE
        buffer[cursor_idx] = c;
        length++;
        buffer[length] = '\0';
        cursor_idx++;
        printf("%c", c);
        fflush(stdout);
      }
    }
  }

  buffer[length] = '\0'; // Ensure final string is safe
  disableRawMode();
  add_history(buffer);
  return buffer;

    #else
    // ---------------------------------------------------------
    // LINUX / MACOS LOGIC
    // ---------------------------------------------------------
      if (c == '\033') { // Escape sequence
        char seq[2];
        // Read the next two bytes immediately
        if (read(STDIN_FILENO, &seq[0], 1) == 0) return buffer;
        if (read(STDIN_FILENO, &seq[1], 1) == 0) return buffer;

        if (seq[0] == '[') { // Its an arrow key
          switch (seq[1]) {

            case 'A': // UP ARROW
              if (history_view_idx > 0) {
                history_view_idx--; // Move backwards
                replace_line(buffer, &length, &cursor_idx, history[history_view_idx], prompt);
              }
              break;

            case 'B': // DOWN ARROW
              if (history_view_idx < history_count) {
                history_view_idx++; // Move fowards

                if (history_view_idx == history_count) {
                  // We moved past the last history item, go to empty line.
                  replace_line(buffer, &length, &cursor_idx, "", prompt);
                }
                else {
                  // Show next history item
                  replace_line(buffer, &length, &cursor_idx, history[history_view_idx], prompt);
                }
              }
              break;

            case 'D': // LEFT ARROW
              if (cursor_idx > 0) {
                cursor_idx--;
                printf("\033[D"); // ANSI code to move cursor left visually
                fflush(stdout);
              }
              break;
            case 'C': // RIGHT ARROW
              if (cursor_idx < length) {
                cursor_idx++;
                printf("\033[C"); // ANSI code to move cursor right visually
                fflush(stdout);
              }
              break;
          }
        }
        continue; // Continue so the \033 doesn't get added to the output buffer.
    }


      /* [UNIX ENTER]
         Reset the cursor_idx and length, move to
         the next line, and break the loop.
      */
      if ((c == '\n' || c == '\r') && buffer[length] != '\\') { // User hit Enter
          buffer[length] = '\0';
          printf("\r\n"); // Move to next line visually
          break;
      }

      /* [UNIX BACKSPACE]
         We do some left buffer shifting, then reprint.
      */
      else if (c == 127) {
        if (cursor_idx > 0) { // Make sure we aren't at the start of the line.

          if (cursor_idx < length) {
            memmove(&buffer[cursor_idx - 1], &buffer[cursor_idx], length - cursor_idx); // Shift buffer left one
          }


          cursor_idx--;
          length--;
          buffer[length] = '\0';
          printf("\b"); // Visual backspace
          printf("%s", &buffer[cursor_idx]); // Print the tail
          printf(" "); // Erase ghost char


          size_t steps_back = (length - cursor_idx) + 1;  //
          for (size_t i = 0; i < steps_back; i++) {       //
            printf("\033[D");                             //  Fix cursor position
          }                                               //

          fflush(stdout);
        }
      }

    /* [NORMAL TYPING - MIDDLE OF STRING]
       We do some right buffer shifting and reprinting.
    */
    else if (cursor_idx < length) {
      // [#1] Shift the memory.
      memmove(&buffer[cursor_idx + 1], &buffer[cursor_idx], length - cursor_idx);

      // Insert the new char
      buffer[cursor_idx] = c;
      length++; // Total string got longer


      // [#2] Redraw the visuals.
      printf("%c", c);
      printf("%s", &buffer[cursor_idx + 1]);


      // [#3] Fix the cursor position.
      size_t steps_back = length - (cursor_idx + 1);
      for (size_t i = 0; i < steps_back; i++) {
          printf("\033[D");
      }

      // Increment our logical cursor position
      cursor_idx++;

      // Flush stdout
      fflush(stdout);
    }

    // [NORMAL TYPING - END OF STRING]
    // Nothing fancy here, just append it and print it.
    else {
      buffer[cursor_idx] = c;
      length++;
      printf("%c", c); // Manual echo
      cursor_idx++;
      fflush(stdout);
    }

    // TODO: Add buffer resizing logic here if position >= bufsize
  }

  disableRawMode(); // Restore terminal for Python
  add_history(buffer);
  return buffer; // Return the pointer to Python
  #endif
}

// --- EXPANSION HELPERS ---

// VARIABLE EXPANSION ($VAR -> Value)
char* expand_variables(const char* input) {
  char *result = malloc(MAX_CMD_LEN);
  const char *p = input;
  char *d = result;

  while (*p) {
    if (*p == '$' && *(p+1) != '(') { // Found $, but not $(
      p++; // Skip $

      // Extract Var Name
      char var_name[128];
      int i = 0;
      while (isalnum(*p) || *p == '_') {
        var_name[i++] = *p++;
      }
      var_name[i] = '\0';

      // Get Value
      char *val = getenv(var_name);
      if (val) {
        strcpy(d, val);
        d += strlen(val);
      }
    } else {
      *d++ = *p++;
    }
  }
  *d = '\0';
  return result;
}

// CAPTURE OUTPUT (Runs a command and returns its stdout)
// Forward declarations for mutual recursion
char* expand_variables(const char* input);
char* expand_subshells(const char* input);

char* capture_command_output(char *cmd) {
  FILE *tmp = tmpfile();
  if (!tmp) return strdup("");

  int tmp_fd = fileno(tmp);

  char *expanded_vars = expand_variables(cmd);
  char *final_cmd = expand_subshells(expanded_vars);

  // Pass the temporary file descriptor directly into the pipeline
  execute_pipeline(final_cmd, STDIN_FILENO, tmp_fd);

  // Clean up allocated strings
  free(expanded_vars);
  free(final_cmd);

  fflush(stdout);
  rewind(tmp);

  char buffer[4096];
  memset(buffer, 0, sizeof(buffer));
  size_t n = fread(buffer, 1, sizeof(buffer) - 1, tmp);
  fclose(tmp);

  if (n > 0 && buffer[n-1] == '\n') {
    buffer[n-1] = '\0';
  } else {
    buffer[n] = '\0';
  }

  return strdup(buffer);
}


// SUBSHELL EXPANSION ($(cmd) -> Output)
char* expand_subshells(const char* input) {
  char *result = malloc(MAX_CMD_LEN);
  const char *p = input;
  char *d = result;

  while (*p) {
    if (*p == '$' && *(p+1) == '(') {
      // Found start of $()
      p += 2; // Skip $(

      // Extract Inner Command
      char sub_cmd[MAX_CMD_LEN];
      int i = 0;
      int paren_depth = 1;

      while (*p && paren_depth > 0) {
        if (*p == '(') paren_depth++;
        if (*p == ')') paren_depth--;

        if (paren_depth > 0) sub_cmd[i++] = *p++;
      }
      p++; // Skip closing )
      sub_cmd[i] = '\0';

      // Execute and Substitute
      char *output = capture_command_output(sub_cmd);
      if (output) {
        strcpy(d, output);
        d += strlen(output);
        free(output);
      }
    } else {
      *d++ = *p++;
    }
  }
  *d = '\0';
  return result;
}

// ASSIGNMENT HANDLING (VAR=VAL)
int handle_assignment(char *input) {
  // Check for '=' before any space
  char *equals = strchr(input, '=');
  char *space = strchr(input, ' ');

  // If '=' exists and is before any space (e.g. "A=B" not "ls --option=B")
  if (equals && (!space || equals < space)) {
    *equals = '\0'; // Split Key and Value
    char *key = input;
    char *val = equals + 1;

    // Using setenv (overwrite=1)
    if (setenv(key, val, 1) != 0) {
      perror("setenv");
    }
    return 1; // Handled
  }
  return 0; // Not an assignment
}

// --- THE ENTRY POINT ---
// Python Usage: shell_core.start(["myshell", "-v"])
static PyObject* shell_start(PyObject *self, PyObject *args) {
  PyObject *py_argv;
  char *prompt = "shell> ";

  // Parse the arguments: we expect a List (O!)
  if (!PyArg_ParseTuple(args, "O!|s", &PyList_Type, &py_argv, &prompt)) {
    return NULL; // Error if not a list
  }

  // Convert Python List -> C argv
  // This allows you to pass flags to your shell if you want to support them later
  int argc = (int)PyList_Size(py_argv);
  char **argv = malloc((argc + 1) * sizeof(char*));

  for (int i = 0; i < argc; i++) {
    PyObject *item = PyList_GetItem(py_argv, i);
    if (!PyUnicode_Check(item)) {
      free(argv);
      PyErr_SetString(PyExc_TypeError, "shell arguments must be strings");
      return NULL;
    }
    // PyUnicode_AsUTF8 returns a pointer to the internal buffer; do not free it
    argv[i] = (char*)PyUnicode_AsUTF8(item);
  }
  argv[argc] = NULL;

  // We hold the GIL by default here.
  // Ideally, release the GIL during get_input() so background threads can run.

  while (1) {
    char *raw_input = get_input(prompt);
    if (raw_input == NULL) break;
    if (strcmp(raw_input, "exit") == 0) {
      free(raw_input);
      break;
    }
    if (strlen(raw_input) == 0) {
      free(raw_input);
      continue;
    }

    // Expand Variables ($VAR -> VAL)
    char *expanded_vars = expand_variables(raw_input);
    free(raw_input);

    // Expand Subshells ($(cmd) -> output)
    char *final_cmd = expand_subshells(expanded_vars);
    free(expanded_vars);

    // Handle Assignment (VAR=VAL)
    // We do this on raw input so expansions don't mess up the assignment syntax
    if (handle_assignment(final_cmd)) {
      free(final_cmd);
      continue; // Skip execution
    }

    // Execute Pipeline
    execute_pipeline(final_cmd, STDIN_FILENO, STDOUT_FILENO);

    free(final_cmd);
  }
  return PyLong_FromLong(0);
}

// --- MODULE REGISTRATION ---

// Update the Method Table
static PyMethodDef ShellMethods[] = {
  {"register",     shell_register,     METH_VARARGS, "Register a command."},
  {"start",        shell_start,        METH_VARARGS, "Start the shell loop."},
  {"get_registry", shell_get_registry, METH_NOARGS,  "List all commands."},
  {"get_command",  shell_get_command,  METH_VARARGS, "Get command function."},
  {NULL, NULL, 0, NULL}
};

static struct PyModuleDef shellmodule = {
  PyModuleDef_HEAD_INIT,
  "shell_core",   // Module name
  NULL,           // Documentation
  -1,             // Global state
  ShellMethods    // Available methods
};

// This is the ONLY function exported to the OS dynamic loader
PyMODINIT_FUNC PyInit_shell_core(void) {
  return PyModule_Create(&shellmodule);
}
