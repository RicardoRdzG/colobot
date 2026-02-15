#!/bin/bash
#
# Git pre-commit hook for Colobot translation validation
# Place this in .git/hooks/pre-commit (make it executable with chmod +x)
#
# This hook validates .po files before committing
# It checks syntax, counts untranslated strings, and looks for common issues
# Works for ALL language translations (not just Spanish)
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo "🔍 Checking translation files..."

# Get list of staged .po files (including in data submodule)
STAGED_PO_FILES=$(git diff --cached --name-only --diff-filter=ACM | grep '\.po$' || true)

if [ -z "$STAGED_PO_FILES" ]; then
    echo -e "${GREEN}✓ No PO files to check${NC}"
    exit 0
fi

ERRORS=0
WARNINGS=0

# Language-specific terminology checks
# Add more languages and terms as needed

for file in $STAGED_PO_FILES; do
    # Skip if file doesn't exist (deleted)
    [ -f "$file" ] || continue
    
    # Get language code from filename (e.g., es.po -> es, fr.po -> fr)
    LANG_CODE=$(basename "$file" .po)
    
    echo "  📄 $file (${LANG_CODE})"
    
    # Validate PO file syntax with msgfmt
    if ! msgfmt -c "$file" -o /dev/null 2>/dev/null; then
        echo -e "${RED}     ✗ Syntax error${NC}"
        msgfmt -c "$file" -o /dev/null || true
        ERRORS=$((ERRORS + 1))
        continue
    fi
    
    # Count untranslated strings (msgstr "")
    UNTRANSLATED=$(grep -c '^msgstr ""$' "$file" 2>/dev/null || echo "0")
    if [ "$UNTRANSLATED" -gt 0 ]; then
        echo -e "${YELLOW}     ⚠ $UNTRANSLATED untranslated strings${NC}"
        WARNINGS=$((WARNINGS + 1))
    fi
    
    # Check for empty msgstr entries (fuzzy translations)
    FUZZY=$(grep -c '#, fuzzy' "$file" 2>/dev/null || echo "0")
    if [ "$FUZZY" -gt 0 ]; then
        echo -e "${YELLOW}     ⚠ $FUZZY fuzzy translations${NC}"
        WARNINGS=$((WARNINGS + 1))
    fi
    
    # Language-specific checks
    case "$LANG_CODE" in
        es)
            # Spanish-specific checks
            # Check for wrong terminology: Célula vs Celda
            if grep -q "Célula" "$file" 2>/dev/null; then
                echo -e "${RED}     ✗ Found 'Célula' - use 'Celda' for power cells${NC}"
                grep -n "Célula" "$file" | head -3
                ERRORS=$((ERRORS + 1))
            fi
            
            # Check for pluralization issues: number + singular noun
            PLURAL_ISSUES=$(grep -E '\b[0-9]+ (trozo|celda|cubo|bot|edificio|mineral)\b' "$file" 2>/dev/null | wc -l)
            if [ "$PLURAL_ISSUES" -gt 0 ]; then
                echo -e "${YELLOW}     ⚠ Possible pluralization issue ($PLURAL_ISSUES matches)${NC}"
                grep -E '\b[0-9]+ (trozo|celda|cubo|bot|edificio|mineral)\b' "$file" | head -2
                WARNINGS=$((WARNINGS + 1))
            fi
            
            # Check for "robot" (should be "bot")
            if grep -qi "robot" "$file" 2>/dev/null; then
                echo -e "${YELLOW}     ⚠ Found 'robot' - consider using 'bot' per game lore${NC}"
                WARNINGS=$((WARNINGS + 1))
            fi
            ;;
            
        fr)
            # French-specific checks could go here
            ;;
            
        de)
            # German-specific checks could go here
            ;;
            
        *)
            # Generic checks for all other languages
            ;;
    esac
done

echo ""
if [ $ERRORS -gt 0 ]; then
    echo -e "${RED}✗ Found $ERRORS error(s). Please fix before committing.${NC}"
    echo ""
    echo "To bypass this check: git commit --no-verify"
    exit 1
fi

if [ $WARNINGS -gt 0 ]; then
    echo -e "${YELLOW}⚠ Found $WARNINGS warning(s).${NC}"
fi

echo -e "${GREEN}✓ PO file validation complete${NC}"
exit 0
