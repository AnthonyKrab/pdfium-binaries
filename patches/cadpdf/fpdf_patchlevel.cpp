// CadPDF additive patch: the level of CadPDF's engine patches in this build.
//
// The app reads it once at start and logs it, so that a run on an old library
// is visible in the log. Raised with every change of the patch set.
//   1 - E1: reduced-size JPEG decoding also for images whose dimensions are
//           not a multiple of the MCU, with the spoiled edge column and row
//           replaced (jpeg_reduced_decode.patch);
//       E2: the decode size of a DCT image is its own size on the device
//           (image_decode_size.patch).
//   2 - C3: a huge JPEG copy cached undecoded after a deep zoom is not reused
//           for a request a reduced-size decode can serve
//           (huge_image_cache.patch).
// A library without this symbol predates level 1.

#include "public/fpdfview.h"

extern "C" {

FPDF_EXPORT int FPDF_CALLCONV FPDF_CadPdfPatchLevel() {
  return 2;
}

}  // extern "C"
