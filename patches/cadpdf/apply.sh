#!/bin/bash -eu
# CadPDF patches.
#  1. fpdfsdk/fpdf_noparse.cpp   - additive, new file.
#  2. fpdfsdk/fpdf_incremental.cpp - additive, new file.
#  3. cpdf_creator_tail.patch    - CPDF_Creator::CreateIncrementalTail() and
#     two conformance fixes in the incremental cross-reference stream
#     (/Type /XRef, endobj). FPDF_SaveAsCopy() paths are otherwise unchanged.
#  4. fpdfsdk/fpdf_patchlevel.cpp - additive, new file: FPDF_CadPdfPatchLevel().
#  5. jpeg_reduced_decode.patch  - E1: reduced-size JPEG decoding also for
#     images not aligned to the MCU, the spoiled edge column and row replaced.
#  6. image_decode_size.patch    - E2: decode size of a DCT image taken from
#     its size on the device, not from the device bitmap.
#  7. huge_image_cache.patch     - C3: a huge JPEG copy cached undecoded is not
#     reused for a request a reduced-size decode can serve.
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
register_source fpdf_patchlevel.cpp

patch --forward -p1 -i "$PATCH_DIR/cpdf_creator_tail.patch" || {
  echo "CadPDF: FAILED to apply cpdf_creator_tail.patch" >&2
  exit 1
}
grep -q "CreateIncrementalTail" core/fpdfapi/edit/cpdf_creator.cpp || {
  echo "CadPDF: cpdf_creator_tail.patch not present after apply" >&2
  exit 1
}
echo "CadPDF: cpdf_creator_tail.patch applied"

apply_change() {
  local PATCH_FILE="$1"
  local TARGET="$2"
  local MARKER="$3"
  patch --forward -p1 -i "$PATCH_DIR/$PATCH_FILE" || {
    echo "CadPDF: FAILED to apply $PATCH_FILE" >&2
    exit 1
  }
  grep -q "$MARKER" "$TARGET" || {
    echo "CadPDF: $PATCH_FILE not present after apply" >&2
    exit 1
  }
  echo "CadPDF: $PATCH_FILE applied"
}

apply_change jpeg_reduced_decode.patch \
  core/fxcodec/jpeg/libjpeg_scanline_decoder.cpp "CadPDF E1"
apply_change image_decode_size.patch \
  core/fpdfapi/render/cpdf_imagerenderer.cpp "CadPDF E2"
apply_change huge_image_cache.patch \
  core/fpdfapi/page/cpdf_pageimagecache.cpp "CadPDF C3"
