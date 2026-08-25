#!/bin/bash -eu
# CadPDF additive patch.
# Adds fpdfsdk/fpdf_noparse.cpp and registers it in fpdfsdk/BUILD.gn.
# Touches no existing PDFium logic. See fpdf_noparse.cpp for the contract.

PATCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cp "$PATCH_DIR/fpdf_noparse.cpp" fpdfsdk/fpdf_noparse.cpp

# Idempotence guard: fail loudly rather than register the file twice.
if grep -q '"fpdf_noparse.cpp"' fpdfsdk/BUILD.gn; then
  echo "CadPDF: fpdf_noparse.cpp already registered in fpdfsdk/BUILD.gn" >&2
  exit 1
fi

# Anchor on the alphabetically adjacent entry. Whitespace-agnostic.
sed -i 's|^\(\s*\)"fpdf_ppo\.cpp",|\1"fpdf_noparse.cpp",\n\1"fpdf_ppo.cpp",|' \
  fpdfsdk/BUILD.gn

# Verification: the build must not proceed silently without our source file.
grep -q '"fpdf_noparse.cpp"' fpdfsdk/BUILD.gn || {
  echo "CadPDF: FAILED to register fpdf_noparse.cpp in fpdfsdk/BUILD.gn" >&2
  exit 1
}

echo "CadPDF: fpdf_noparse.cpp applied and registered"
