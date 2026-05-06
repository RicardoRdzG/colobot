# Colobot: Gold Edition - AI Agent Guide

This file provides essential information for AI coding agents working in the Colobot repository.

## Quick Start Commands

### Building the Project
```bash
# Configure with CMake preset (recommended for CI parity)
cmake --preset Linux-CI-gcc  # or Linux-CI-clang, MacOS-CI, Windows-CI

# Build the project
cmake --build --preset Linux-CI-gcc -- -j$(nproc)

# Install
cmake --build --preset Linux-CI-gcc --target install

# Run the game
./build/colobot  # or <install-prefix>/games/colobot
```

### Running Tests
```bash
# Run all tests
cd build && ctest --output-on-failure

# Run single test file (for specific testing)
./Colobot-UnitTests --gtest_filter="YourTestSuite.YourTestMethod"

# Run tests with XML output (CI format)
./Colobot-UnitTests --gtest_output=xml:test-results.xml
```

### Running with Translations
```bash
# Full translation (UI + help + levels) - replace 'es' with desired language
export LANGUAGE=es
./build/colobot -datadir install/data --langdir install/lang

# Help and levels only (from data submodule, no UI)
./build/colobot -datadir data
```

### Code Quality Checks
```bash
# Run linter (requires colobot-lint tool)
./scripts/git-diff-check-colobot-lint.sh

# Build with lint checking enabled
cmake --preset Linux-CI-gcc -DCOLOBOT_LINT_BUILD=ON
```

## Project Structure

### Core Components
- **colobot-common/**: Shared utilities and low-level helpers
- **CBot/**: In-game scripting language parser/runtime (NO exceptions allowed)
- **colobot-base/**: Game engine, physics, objects, graphics
- **colobot-app/**: Main colobot executable (entrypoint)
- **tools/**: Development and build tools
- **desktop/**: Platform-specific packaging
- **test/**: Unit tests using GoogleTest framework

### Key Files
- **CMakePresets.json**: Canonical build presets used by CI
- **vcpkg.json**: Dependencies specification
- **CMakeLists.txt**: Main project configuration
- **data/**: Game data (git submodule from colobot-data)
- **po/**: Main UI translations (`.po` files, compiled to `.mo` at build time)
- **install/data/**: Compiled game data (from build process)
- **install/lang/**: Compiled UI translations (gettext `.mo` files)

## Build Options

### Useful CMake Variables
- `TESTS=ON/OFF`: Enable/disable unit tests
- `TOOLS=ON/OFF`: Build development tools
- `DESKTOP=ON/OFF`: Build desktop integration
- `COLOBOT_LINT_BUILD=ON/OFF`: Enable lint checking during build
- `CBOT_STATIC=ON/OFF`: Static linking for CBot
- `PORTABLE=ON/OFF`: Portable build mode
- `PORTABLE_SAVES=ON/OFF`: Portable save file location

## Code Style Guidelines

### Formatting
- **Indentation**: 4 spaces (NO tabs)
- **Braces**: Opening brace on new line
- **Line endings**: Unix (LF)
- **Trailing whitespace**: Not allowed

### Naming Conventions
- **Functions**: PascalCase (e.g., `FooBar()`)
- **Classes**: Prefix with 'C' (e.g., `class CSomeClass`)
- **Accessors**: `SetValue()`, `GetValue()`
- **Structs/Enums**: PascalCase (e.g., `struct SomeStruct`)
- **Enum values**: PREFIX_ALL_CAPS (e.g., `SOME_ENUM_VALUE`)
- **Constants**: ALL_CAPS (e.g., `const int MAX_SPACE = 1000;`)

### C++ Style Rules
- Use C++20 standard
- Use C++-style casts: `static_cast<Type>(value)` instead of `(Type)value`
- No global variables - use static class members
- Prefer `const` over `#define`
- Use STL classes where appropriate
- Exceptions allowed except in CBot code (memory management concerns)
- Provide full namespace qualifiers: `Math::MultiplyMatrices`

### Include Order
1. Associated header (in .cpp files): `#include "app/app.h"`
2. Local includes (alphabetical): `#include "common/logger.h"`
3. System includes: `#include <vector>`, `#include <SDL/SDL.h>`

### Documentation
- Document public APIs with Doxygen comments
- Comments should explain **why**, not **what**
- Write unit tests for new code in `test/` subdirectories

## Development Workflow

### Making Changes
1. Ensure code follows style guidelines
2. Write/update unit tests
3. Run linter locally
4. Build and test with CI presets
5. Keep commits small and focused

### Testing Strategy
- Unit tests use GoogleTest framework
- Tests discovered automatically via `gtest_discover_tests`
- CI runs tests with `-Werror` (treat warnings as errors)
- Place tests near the code they test

### Testing Patterns

#### Testing Protected/Private Methods
**CRITICAL: Friend class declarations do NOT work with Google Test**

The `TEST_F` macro creates a derived test class that does NOT inherit friend access from the base test fixture. Even with correct syntax and successful compilation, friend access will not work at runtime.

**Correct Pattern: Wrapper Class**

Use inheritance to expose protected methods as public:

```cpp
// In test file
class CTextWrapper : public CText {
public:
    explicit CTextWrapper(CEngine* engine) : CText(engine) {}
    
    // Expose protected method for testing
    glm::ivec2 GetNextTilePosForTest(const FontTexture& ft) { 
        return GetNextTilePos(ft); 
    }
    
    // Expose protected static method for testing
    static uint64_t PackTileSizeForTest(const glm::ivec2& tileSize) {
        return PackTileSize(tileSize);
    }
};
```

**Reference Implementation**: See `CApplicationWrapper` in `test/src/app/app_test.cpp`

#### HippoMocks Limitations

HippoMocks is the mocking framework used in this project. However, it has limitations with complex classes:

- **Complex dependencies**: Classes like `CEngine` with many virtual methods and complex state are difficult to mock reliably
- **Mock setup errors**: "Function called without expectation!" errors can occur even with seemingly correct setup
- **Alternative approaches**: For complex classes, consider:
  1. Integration tests instead of unit tests
  2. Refactoring to inject dependencies (dependency injection)
  3. Testing at a higher level of abstraction

**Reference Implementation**: See `test/src/graphics/engine/lightman_test.cpp` for successful HippoMocks usage

#### Moving Internal Structures for Testing

Sometimes internal structures need to be moved from anonymous namespaces in .cpp files to .h files for test access:

1. **Before** (in text.cpp):
```cpp
namespace {
    struct FontTexture { ... };
}
```

2. **After** (in text.h):
```cpp
// In header, outside anonymous namespace
struct FontTexture { ... };
```

3. **Test file** can now use:
```cpp
#include "graphics/engine/text.h"
// FontTexture is now accessible
```

**Trade-off**: This slightly breaks encapsulation but enables proper unit testing of internal logic.

### Memory Management
- RAII preferred throughout codebase
- **No exceptions in CBot code** (manual memory management)
- Use smart pointers and STL containers

## CI Configuration

### Presets by Platform
- **Linux-CI-gcc**: GCC-based Linux builds
- **Linux-CI-clang**: Clang-based Linux builds  
- **MacOS-CI**: macOS builds
- **Windows-CI**: MSVC-based Windows builds

### CI Requirements
- Builds with `-Werror` (warnings as errors)
- Runs full unit test suite
- Generates documentation with Doxygen
- Uploads test results as XML

## Important Notes

### Game Data Requirements
- Game data lives in separate submodule (`data/`)
- Submodule **must** be present for builds/tests
- Use `git submodule update --init --recursive` after cloning

### Dependencies
- System packages preferred, fallback to vcpkg
- See `INSTALL.md` for platform-specific requirements
- CI uses vcpkg for consistency

### Performance Considerations
- Debug builds: configure with `-DCMAKE_BUILD_TYPE=Debug`
- Use `-loglevel debug` for debugging output
- Profile builds use `-DCMAKE_BUILD_TYPE=RelWithDebInfo`

## Translation System

### Translation Branches
- **Main repo**: `merge-spanish-font-fix` - combines font fixes + translation data submodule
- **Data submodule**: `dev-spanish-review` - all translation files (PO, MO, and compiled TXT)
- **Progress tracker**: `data/SPANISH_TODO.md` - tracks manual review progress of translation files

### Translation Workflow
```bash
# 1. Work in data submodule
cd data
git checkout dev-spanish-review

# 2. After editing PO files, compile and test
for f in $(find . -name "*.po"); do msgfmt -o "${f%.po}.mo" "$f"; done

# 3. Commit and push
git add <reviewed_files>
git commit -m "Add manually reviewed <language> translation for <category>"
git push origin dev-spanish-review

# 4. Update main repo submodule ref
cd ..
git add data
git commit -m "Update data submodule: <description>"
git push origin merge-spanish-font-fix
```

### Language Codes
Each language uses a specific letter code for help/level files:
- English: `E` (help.E.txt, scene.E.txt)
- German: `D`
- French: `F`
- Spanish: `S` (uses `S` not `E` to avoid collision)
- Hungarian: `H`
- Polish: `P`
- Czech: `C`
- Russian: `R`
- Portuguese: `Pt`

### Running with Translations
```bash
# Full translation (UI + help + levels) - replace 'es' with desired language
export LANGUAGE=es
./build/colobot -datadir install/data --langdir install/lang

# Help and levels only (from data submodule, no UI)
./build/colobot -datadir data
```

### Key Locations
- **Main UI**: `po/<lang>.po` / `install/lang/<lang>/LC_MESSAGES/colobot.mo`
- **Help files**: `data/help/<letter>/` (compiled TXT), `data/help/*/po/<lang>.po` (PO source)
- **Level files**: `data/levels/*/chapter00X/level00Y/help/help.<letter>.txt`
- **Level PO source**: `data/levels/*/po/<lang>.po`

### Translation File Types
| Type | Purpose | Location |
|------|---------|----------|
| `.po` | Source translation file (editable) | `data/*/po/<lang>.po` |
| `.mo` | Compiled binary for gettext | `install/lang/<lang>/LC_MESSAGES/` |
| `.txt` | Compiled help text for in-game help | `data/help/<letter>/` |

### Terminology Rules
- **Use "bot"** (never "robot") - part of game lore (COLO-BOT)
- **Energy cells**: "celdas de energía" (NOT "células de energía")
- **Code keywords**: Keep English (`turn()`, `fire()`, `motor()`, `while`, `if`, `radar`)
- **Tags preserved**: `<c/>`, `<n/>`, `<button 22/>`, `<code>...</code>`

### PO File Format
```po
#. type: Plain text
#: ../help/help.E.txt:2
#, no-wrap
msgid "Objective"
msgstr "Objectif"  # or "Objetivo" for Spanish, etc.
```

### Validation
```bash
# Validate PO syntax
msgfmt -c po/<lang>.po

# Check empty translations
grep -c '^msgstr ""$' po/<lang>.po

# Check all translation files exist
find data -name "<lang>.po" | wc -l
find install/data/help/<letter>/ -name "*.txt" | wc -l
ls install/lang/<lang>/LC_MESSAGES/
```

### Common Fixes
```bash
# Compile all PO files to MO
cd data && for f in $(find . -name "*.po"); do msgfmt -o "${f%.po}.mo" "$f"; done

# Fix terminology (example for Spanish)
find . -name "*.po" -exec sed -i 's/células de energía/celdas de energía/g' {}

# Find untranslated strings
grep '^msgstr ""$' data/po/*.po | wc -l
```

### Build and Test (with Translations)
```bash
# Build
cd /home/rrodriguez/Documentos/GitHub/colobot
cmake --build --preset Linux-CI-gcc -- -j$(nproc)

# Test translations (run game with UI + help + levels)
export LANGUAGE=es
./build/colobot -datadir install/data --langdir install/lang

# Verify translations loaded:
./build/colobot -datadir install/data --langdir install/lang -loglevel debug 2>&1 | grep -i "locale\|lang"
```

### Validation
```bash
# Validate PO syntax
msgfmt -c po/<lang>.po

# Check empty translations
grep -c '^msgstr ""$' po/<lang>.po

# Check all translation files exist
find data -name "<lang>.po" | wc -l
find data -name "<lang>.mo" | wc -l
ls install/data/help/<letter>/             # Help files
ls install/lang/<lang>/LC_MESSAGES/        # UI translation
```

### Common Fixes
```bash
# Compile PO to MO for testing
cd data && for f in $(find . -name "<lang>.po"); do msgfmt -o "${f%.po}.mo" "$f"; done

# Find untranslated strings
grep '^msgstr ""$' data/po/<lang>.po | wc -l
```

## Font Texture System (Per-Atlas Sizing)

### Architecture Overview

Colobot's text rendering uses texture atlases to store rendered character glyphs. Each atlas is a GPU texture containing a grid of character tiles.

**Before (global sizing):** All atlases shared a single global size (`m_fontTextureSize`). When a character didn't fit, the entire system resized (destroying all atlases). The resize code was dead - it never triggered.

**After (per-atlas sizing):** Each `FontTexture` has its own `textureSize`. Atlases are sized independently based on their tile requirements. When an atlas fills up, a new atlas is created.

### Key Data Structures

```cpp
struct CharTexture {
    unsigned int id = 0;       // OpenGL texture ID
    glm::ivec2 charPos;        // Position in atlas (pixels)
    glm::ivec2 charSize;       // Character dimensions (pixels)
    glm::ivec2 atlasSize;      // Atlas dimensions for UV calculation (must never be zero)
};

struct FontTexture {
    unsigned int id = 0;       // OpenGL texture ID
    glm::ivec2 tileSize;       // Size of each character tile (pixels)
    glm::ivec2 textureSize;    // Size of this atlas (pixels)
    int freeSlots = 0;         // Available tile slots remaining
};
```

### Atlas Storage

Atlases are stored in `std::unordered_map<uint64_t, std::vector<FontTexture>>` keyed by packed tile size. This provides O(1) lookup for finding an atlas with matching tile dimensions.

```cpp
static uint64_t PackTileSize(const glm::ivec2& tileSize) {
    return (static_cast<uint64_t>(tileSize.x) << 32) | static_cast<uint64_t>(tileSize.y);
}
```

### Atlas Size Calculation

```cpp
constexpr int SLOTS_PER_ROW = 16;           // 16x16 = 256 slots minimum
int minSize = tileSize.x * SLOTS_PER_ROW;   // Minimum atlas dimension
int initialSize = Math::NextPowerOfTwo(minSize);
initialSize = std::max(initialSize, FONT_TEXTURE_BASE_SIZE);  // 256
initialSize = std::min(initialSize, FONT_TEXTURE_MAX_SIZE);    // 2048
```

All atlas sizes are power-of-two for compatibility with older GPUs (OpenGL 2.x requirement).

### Testing Patterns for Font System

The `CTextWrapper` pattern exposes protected methods for testing:
```cpp
class CTextWrapper : public CText {
public:
    explicit CTextWrapper(CEngine* engine) : CText(engine) {}
    
    glm::ivec2 GetNextTilePosForTest(const FontTexture& ft) {
        return GetNextTilePos(ft);
    }
    
    static uint64_t PackTileSizeForTest(const glm::ivec2& tileSize) {
        return PackTileSize(tileSize);
    }
};
```

### Build, Test, Install, Execute

```bash
# Build
cd /home/rrodriguez/Documentos/GitHub/colobot
cmake --build --preset Linux-CI-gcc -- -j$(nproc)

# Test
cd build
./Colobot-UnitTests                           # All tests (239)
./Colobot-UnitTests --gtest_filter="*Atlas*"  # Atlas tests (8)
./Colobot-UnitTests --gtest_filter="*HiDPI*"  # HiDPI tests (13)

# Install
cmake --build --preset Linux-CI-gcc --target install

# Execute with translations (replace 'es' with desired language)
export LANGUAGE=es
./colobot -datadir install/data --langdir install/lang -loglevel debug
```

### UV Coordinates (Texture Coordinates)

UV coordinates map a position on screen to a position in a texture:
- **U** = horizontal (0.0 left, 1.0 right)
- **V** = vertical (0.0 top, 1.0 bottom)

Calculated as: `u = charPos.x / atlasSize.x`, `v = charPos.y / atlasSize.y`

Each `CharTexture` stores `atlasSize` (instead of looking it up from `FontTexture`) to avoid pointer chasing and O(n) atlas searches during rendering.

## Getting Help

- **Documentation**: `docs/` directory, `INSTALL.md`
- **Translation Guide**: `docs/TRANSLATIONS.md`
- **Issues**: GitHub issue tracker
- **Discussions**: GitHub Discussions
- **Real-time**: Discord server (linked in README)

When in doubt, prefer conservative changes and coordinate major modifications through issues or Discord discussions.