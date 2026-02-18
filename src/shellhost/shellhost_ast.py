# shellhost_ast.py

import sys
import shellparser
from .shellhost_io import JobIO

class BaseNode:
    """Base class for streams to allow polymorphism between Commands and Subshells."""
    def __init__(self):
        self.stdin = sys.stdin
        self.stdout = sys.stdout
        self.stderr = sys.stderr

class CommandNode(BaseNode):
    def __init__(self):
        super().__init__()
        self.arguments = [] # Can contain strings OR CommandSubstitutionNodes

    def __repr__(self):
        # We process arguments to show which are nodes vs strings
        args_repr = []
        for arg in self.arguments:
            if isinstance(arg, CommandSubstitutionNode):
                args_repr.append(str(arg))
            else:
                args_repr.append(arg)

        return (f"[Command] {' '.join(args_repr)}\n"
                f"   in: {self.stdin}, out: {self.stdout}, err: {self.stderr}")

class CommandSubstitutionNode:
    def __init__(self, inner_ast):
        self.inner_ast = inner_ast # This is a full command/pipeline AST

    def __repr__(self):
        return f"$({self.inner_ast})"

    def __str__(self):
        return f"$({self.inner_ast})"


class SubshellNode(BaseNode):
    def __init__(self, children):
        super().__init__()
        self.children = children  # List of nodes inside the parentheses

    def __repr__(self):
        # Indent children for visibility
        child_str = '\n'.join([f"    {str(c).replace(chr(10), chr(10)+'    ')}" for c in self.children])
        return (f"[Subshell]\n{child_str}\n"
                f"   in: {self.stdin}, out: {self.stdout}, err: {self.stderr}")

def build_ast(tokens):
    # The stack holds lists of nodes.
    # stack[0] is the main script. stack[-1] is the current scope.
    stack = [ [] ]

    # State to track if the *next* command should receive piped input
    next_stdin_source = None

    i = 0
    while i < len(tokens):
        token = tokens[i]

        # --- 1. Subshell Start ---
        if token == '(':
            # Start a new scope
            stack.append([])
            # If a pipe was leading into this subshell, set the flag on the scope
            # (We will apply it to the first command inside later, or store it as metadata)
            # For simplicity, we carry the pipe state into the first node created inside.
            pass

        # --- 2. Subshell End ---
        elif token == ')':
            if len(stack) < 2:
                raise SyntaxError("Unbalanced parentheses: too many ')'")

            # Pop the current scope's nodes
            inner_nodes = stack.pop()

            # Create a SubshellNode containing them
            subshell = SubshellNode(inner_nodes)

            # Apply pipe input if pending
            if next_stdin_source:
                subshell.stdin = next_stdin_source
                next_stdin_source = None

            # Add this subshell to the parent scope
            stack[-1].append(subshell)

        # --- 3. Pipe ---
        elif token == '|':
            if not stack[-1]:
                raise SyntaxError("Pipe '|' with no previous command")

            # The *last* node in the current scope pipes its output
            stack[-1][-1].stdout = 'PIPE_OUT'

            # The *next* node we create will read from this pipe
            next_stdin_source = 'PIPE_IN'

        # --- 4. Redirections ---
        elif token in ('>', '>>', '2>', '<'):
            if not stack[-1]:
                raise SyntaxError(f"Redirection '{token}' with no previous command")

            # We must have a target file next
            if i + 1 >= len(tokens):
                raise SyntaxError(f"Missing target for '{token}'")

            target = tokens[i+1]
            i += 1 # Consume the file token

            # Apply to the LAST node in the current scope
            # (This works for both CommandNodes AND SubshellNodes!)
            active_node = stack[-1][-1]

            if token == '>':
                active_node.stdout = open(target, 'w')
            elif token == '>>':
                active_node.stdout = open(target, 'a') # 'a' for Append
            elif token == '2>':
                active_node.stderr = open(target, 'w')
            elif token == '2>>':
                active_node.stderr = open(target, 'a')
            elif token == '<':
                active_node.stdin = open(target, 'r')

        # --- 5. Regular Command / Arguments ---
        else:
            # Check if we are starting a NEW command or adding args to an existing one

            # We start a new command if:
            # 1. The current scope is empty
            # 2. The last node was a Subshell (you can't add string args to a subshell object)
            # 3. The last node's stdout is redirected (implies the command ended, though shell logic varies here)
            # 4. We have a pending pipe input

            # --- Handle Command Arguments ---
            # (This is the only part that changes)
            if token not in ('(', ')', '|', '>', '>>', '2>', '<'):
                # Logic to determine if we start a new command or append to existing
                start_new = False
                if not stack[-1]: start_new = True
                elif isinstance(stack[-1][-1], SubshellNode): start_new = True
                elif next_stdin_source: start_new = True

                # PARSE THE ARGUMENT
                parsed_arg = parse_argument(token)

                if start_new:
                    cmd = CommandNode()
                    cmd.arguments.append(parsed_arg)
                    if next_stdin_source:
                        cmd.stdin = next_stdin_source
                        next_stdin_source = None
                    stack[-1].append(cmd)
                else:
                    stack[-1][-1].arguments.append(parsed_arg)

        i += 1

    if len(stack) > 1:
        raise SyntaxError("Unbalanced parentheses: missing ')'")

    return stack[0]


def parse_argument(token):
    """
    Check if a token is a command substitution.
    If yes, return a CommandSubstitutionNode.
    If no, return the string.
    """
    if token.startswith('$(') and token.endswith(')'):
        # 1. Extract the inner command string
        inner_content = token[2:-1]

        # 2. Tokenize the inner content (Simple split for demonstration)
        # In a real shell, you'd need a robust tokenizer here.
        inner_tokens = shellparser.parse_args(inner_content)

        # 3. Recursively build the AST for the inner command
        inner_ast = build_ast(inner_tokens)

        return CommandSubstitutionNode(inner_ast)
    return token

# The Post-Processing Linker
def link_pipes(nodes):
    for i in range(len(nodes) - 1):
        curr = nodes[i]
        nxt = nodes[i+1]

        # Link Pipe Out -> Pipe In
        if curr.stdout == 'PIPE_OUT' and nxt.stdin == 'PIPE_IN':

            connection = JobIO()

            curr.stdout = connection
            nxt.stdin = connection

    # Recurse into subshells
    for node in nodes:
        if isinstance(node, SubshellNode):
            link_pipes(node.children)
    return nodes
