#!/bin/bash -eu
# CadPDF patches.
#  1. fpdfsdk/fpdf_noparse.cpp   - additive, new file.
#  2. fpdfsdk/fpdf_incremental.cpp - additive, new file.
#  3. cpdf_creator_tail.patch    - CPDF_Creator::CreateIncrementalTail() and
#     two conformance fixes in the incremental cross-reference stream
#     (/Type /XRef, endobj). FPDF_SaveAsCopy() paths are otherwise unchanged.
# Every step fails loudly; the build must never proceed with a partial set.

PATCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

register_source() {
  local FILE="$1"
  cp "$PATCH_DIR/$FILE" "fpdfsdk/$FILE"
  if grep -q "\"$FILE\"" fpdfsdk/BUILD.gn; then
    echo "CadPDF: $FILE already registered in fpdfsdk/BUILD.gn" >&2
    exit 1
  fi
  # Anchor on an existing entry. Whitespace-agnostic.
  sed -i "s|^\(\s*\)\"fpdf_ppo\.cpp\",|\1\"$FILE\",\n\1\"fpdf_ppo.cpp\",|" \
    fpdfsdk/BUILD.gn
  grep -q "\"$FILE\"" fpdfsdk/BUILD.gn || {
    echo "CadPDF: FAILED to register $FILE in fpdfsdk/BUILD.gn" >&2
    exit 1
  }
  echo "CadPDF: $FILE applied and registered"
}

register_source fpdf_noparse.cpp
register_source fpdf_incremental.cpp

patch --forward -p1 -i "$PATCH_DIR/cpdf_creator_tail.patch" || {
  echo "CadPDF: FAILED to apply cpdf_creator_tail.patch" >&2
  exit 1
}
grep -q "CreateIncrementalTail" core/fpdfapi/edit/cpdf_creator.cpp || {
  echo "CadPDF: cpdf_creator_tail.patch not present after apply" >&2
  exit 1
}
echo "CadPDF: cpdf_creator_tail.patch applied"
