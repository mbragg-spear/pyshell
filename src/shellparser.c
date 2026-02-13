#ifdef _WIN32
    #include <conio.h>
    #include <windows.h>
#else
    #include <termios.h>
    #include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <Python.h>

#define MAX_HISTORY 50
#define MAX_CMD_LEN 256
#define PY_SSIZE_T_CLEAN



/* ==========================================
 * 1. EXTERNAL C FUNCTION DECLARATIONS
 * ========================================== */
extern int parse_args(const char* input_str, char args[32][256]);
extern void free_mem(void* ptr);
extern char* get_input(const char* prompt);
extern void add_history(const char* cmd);


/* ==========================================
 * 2. PYTHON WRAPPERS
 * ========================================== */

// Wrapper for: int parse_args(...)
// Python usage: result_list = parse_args("cmd arg1 arg2")
static PyObject* shellparser_parse_args(PyObject *self, PyObject *args) {
  const char *input_str;
  char parsed_args[32][256];  // Allocate buffer on stack for C function
  int count;

  // 1. Parse Python String -> C String
  if (!PyArg_ParseTuple(args, "s", &input_str)) {
    return NULL;
  }

  // 2. Call the actual C function
  count = parse_args(input_str, parsed_args);

  // 3. Convert C Array -> Python List
  PyObject* py_list = PyList_New(count);
  for (int i = 0; i < count; i++) {
    PyObject* py_str = PyUnicode_FromString(parsed_args[i]);
    PyList_SetItem(py_list, i, py_str); // Steals reference to py_str
  }

  return py_list;
}



// Wrapper for: char* get_input(...)
// Python usage: user_input = get_input("Enter command: ")
static PyObject* shellparser_get_input(PyObject *self, PyObject *args) {
  const char *prompt;
  char *result;

  if (!PyArg_ParseTuple(args, "s", &prompt)) {
    return NULL;
  }

  // Call C function
  result = get_input(prompt);

  // Convert C String -> Python String
  PyObject* py_result = PyUnicode_FromString(result);

  // Python now has its own copy, so its safe to free() from here.
  free_mem(result);

  return py_result;
}


// Wrapper for: void add_history(...)
// Python usage: add_history("ls -la")
static PyObject* shellparser_add_history(PyObject *self, PyObject *args) {
  const char *cmd;

  if (!PyArg_ParseTuple(args, "s", &cmd)) {
    return NULL;
  }

  add_history(cmd);

  // Return None (void functions must return Py_None in Python C API)
  Py_RETURN_NONE;
}

// Wrapper for: void free_mem(...)
// Python usage: free_mem(123456)  <-- Takes an integer address
// Likely not needed anymore, but I'm not gonna trash it just yet.
static PyObject* shellparser_free_mem(PyObject *self, PyObject *args) {
  unsigned long long ptr_addr;

  if (!PyArg_ParseTuple(args, "K", &ptr_addr)) { // K = unsigned long long
    return NULL;
  }

  free_mem((void*)ptr_addr);

  Py_RETURN_NONE;
}

/* ==========================================
 * 3. THE METHOD TABLE
 * ========================================== */
static PyMethodDef ShellParserMethods[] = {
  {"parse_args",  shellparser_parse_args,  METH_VARARGS, "Parse a command string into a list of arguments."},
  {"get_input",   shellparser_get_input,   METH_VARARGS, "Get input from user with a prompt."},
  {"add_history", shellparser_add_history, METH_VARARGS, "Add a command to history."},
  {"free_mem",    shellparser_free_mem,    METH_VARARGS, "Free a C pointer (Pass address as int)."},
  {NULL, NULL, 0, NULL}  /* Sentinel */
};


/* ==========================================
 * 4. MODULE INITIALIZATION
 * ========================================== */
static struct PyModuleDef shellparser_module = {
  PyModuleDef_HEAD_INIT,
  "shellparser",   /* name of module */
  NULL,            /* module documentation */
  -1,              /* size of per-interpreter state */
  ShellParserMethods
};

PyMODINIT_FUNC
PyInit_shellparser(void) {
  return PyModule_Create(&shellparser_module);
}

// Global History Storage
static char history[MAX_HISTORY][MAX_CMD_LEN];
static int history_count = 0;
static int history_view_idx = 0; // Where the user is currently looking

// Python calls this to save a command
void add_history(const char* cmd) {
  /* Helper function that adds a line of text to the shell command history.

  Args:
    cmd: the command string to append to the history

  Returns:
    None
  */
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
  /* Helper function that replaces the current line of text with a different one.

  Args:
    buffer: the current text buffer
    length: the size of the current buffer
    cursor_idx: the position of the cursor within the text
    new_text: the text to replace the current line with
    term_prompt: the terminal prompt to print before replacing the line

  Returns:
    None
  */
  // 1. Fix visuals
  printf("\r");         // Move to line start
  printf("\x1b[K");     // Print ANSI code to clear the line
  printf("%s", term_prompt);  // Print prompt
  printf("%s", new_text); // Print buffer content

  // 2. Update Buffer Memory
  strcpy(buffer, new_text);
  *length = strlen(buffer);
  *cursor_idx = *length; // Set cursor to end of line

  fflush(stdout);
}

/*
Handle the setup for how we can
enable/disable raw input mode
across platforms.
*/

// WINDOWS IMPLEMENTATION
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

// LINUX / MACOS IMPLEMENTATION
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


// Exported function callable from Python
char* get_input(const char* prompt) {
  /* Handles keyboard input and special keys.

  Args:
    prompt: character array for the shell prompt.


  Returns:
    The character array buffer of user input.
  */
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
          // Skip the rest of the loop!
          // Do not let 'c' (0xE0) or 'special' (75) get added to buffer.
          continue;
      }

      // --- 2. HANDLE ENTER ---
      // Windows sends \r (13) for Enter
      if (c == '\r') {
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
      if (c == '\n' || c == '\r') { // User hit Enter
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
  return buffer; // Return the pointer to Python
  #endif
}

void free_line(char* line) {
    free(line);
}

// 2. The Liberator
void free_mem(void* ptr) {
    /* Wrapper around the standard free() call. Using this is safer than calling libc.free from Python.

    Args:
      ptr: the pointer to what we're freeing.

    Returns:
      None
    */
    if (ptr) free(ptr);
}


int parse_args(const char* input_str, char args[32][256]) {
  /* Takes an input string and splits it into individual char arrays.

  Args:
    input_str: The string to split.
    args: A pre-allocated 32x256 2D character array to be filled with the individual parts of the string.

  Returns:
    A int which indicates how many individual parts were found.

  */


  // Constant size of our input string.
  const size_t input_size = strlen(input_str);

  // Buffers/sizes for handling characters during iteration.
  size_t arg_position = 0; // For keeping track of what argument number we're at (0-16).
  size_t buff_position = 0; // For keeping track of position within arg_buffer (0-255).
  char arg_buffer[256];
  char current_char;

  // Boolean values for tracking state.
  bool single_quote = false;
  bool double_quote = false;
  bool char_escaped = false;


  for (size_t i = 0; i < input_size; i++) {
    current_char = input_str[i];

    /* [#0] Are we gonna overflow the buffer?
           This is a pre-check on each iteration to make sure
           buff_position doesn't go out of bounds.
    */
    if (buff_position >=  255) {
      fprintf(stderr, "Error: Argument exceeds maximum buffer size.\n");
      return -1;
    }


    /* [#1] Are we are currently escaped?
           This one is easy because if this boolean is
           true, when we just append to the arg buffer.
    */
    if (char_escaped) {
      arg_buffer[buff_position] = current_char;
      buff_position++;
      char_escaped = false;
    }

    /* [#2] Is this character whitespace?
           For this check, we'll need to then also check
           if we are currently quoted in any way.

           - If we are quoted, append to the current arg buffer.

           - If we are not quoted, append the current arg buffer
             to args before flushing.
    */
    else if (current_char == ' ' || current_char == '\n') {

      // Quote check
      if (single_quote || double_quote) {
        arg_buffer[buff_position] = current_char;     // - Append to arg_buffer.
        buff_position++;                              // - Increment buffer position.
      }
      else {
        arg_buffer[buff_position] = '\0';                    // - Manually ensure null terminator.
        snprintf(args[arg_position], 256, "%s", arg_buffer); // - Replaced strncpy with snprintf so the compiler would shut up
        memset(arg_buffer, 0, 256);                          // - Flush arg_buffer.
        buff_position = 0;                                   // - Reset buffer position.
        arg_position++;                                      // - Increment arg position.
      }

    }

    /* [#3] Is this character a single/double quote?
           If this check is true and we are not
           already inside a quote, we'll be
           inverting the associated boolean value.

           I am including the check for both types of
           quotes in #3 because they are basically
           the same thing.
    */
    else if (current_char == '\'' && !double_quote) {
      single_quote = !single_quote;
    }

    else if (current_char == '\"' && !single_quote) {
      double_quote = !double_quote;
    }
    /* [#6] Is this character a backslash?
           If the character is a backslash, then
           we ignore it and set the boolean char_escaped.

    */
    else if (current_char == '\\') {
      char_escaped = true;
    }

    /* [FINAL] No check here.
           If we've made it to this point, then
           its just another character in the
           current arg. Just append it and
           move on.
    */
    else {
      arg_buffer[buff_position] = current_char;
      buff_position++;
    }

  }

  /* [CLEAN UP] Do we have anything left in buffers?
         If there is anything left in arg_buffer, append it
         to args and flush the buffer.
  */
  if (buff_position > 0) {
    arg_buffer[buff_position] = '\0';                    // - Manually ensure null terminator.
    snprintf(args[arg_position], 256, "%s", arg_buffer); // - Replaced strncpy with snprintf so the compiler would shut up
    memset(arg_buffer, 0, 256);                          // - Flush arg_buffer.
    buff_position = 0;                                   // - Reset buffer position.
    arg_position++;                                      // - Increment arg position.
  }

  return arg_position;
}
