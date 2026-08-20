# Formatting & Style Rules

All C++ and C code in this repository adheres to the formatting configuration in [`.clang-format`](file:///c:/Users/Bailen/Documents/programming/AntiGravity/horde/.clang-format):

- **Indentation**: 4 spaces, never tabs. Case labels indented.
- **Braces**: Attached on the same line (`BreakBeforeBraces: Attach`).
- **Pointer/Reference Alignment**: Left-aligned (`int* p`, `const Type& ref`).
- **Column Limit**: 120 columns.
- **Include Ordering**:
  1. `<SDL3...>`
  2. Third-party headers (`glm`, `imgui`)
  3. C system headers (`<*.h>`)
  4. Standard library (`<vector>`, `<string>`, etc.)
  5. Project-local headers (`"..."`)
- **Spacing**: No space after `template` keyword (`template<typename T>`), break template declarations.
