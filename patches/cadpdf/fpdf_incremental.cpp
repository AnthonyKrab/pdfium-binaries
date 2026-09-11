// CadPDF additive patch: incremental update section ("tail") writer and
// object-number accessors for callers that build the object list.
//
// CONTRACT - violating any item corrupts documents:
//   * FPDF_SaveIncrementalTail() writes ONLY an update section: every object
//     numbered above the file's last object number, the objects listed by the
//     caller, a cross-reference section of the same kind as the file's last
//     one, and a trailer with /Prev. The original bytes are NOT written.
//   * The result is valid only when appended to the exact file the document
//     was loaded from, at the length it had when loaded. The document must be
//     reloaded before the next call: /Prev and offsets come from the parser.
//   * Listing every modified EXISTING object is the caller's duty. An omitted
//     one is silently lost. New objects are included automatically.
//   * On any refusal nothing is written. Output reaches |file_write| in one
//     WriteBlock() call, only after it is complete and checked.
//
// Not XFA-aware by design; CadPDF builds PDFium without XFA.

#include <stdint.h>
#include <string.h>

#include <vector>

#include "core/fpdfapi/edit/cpdf_creator.h"
#include "core/fpdfapi/page/cpdf_page.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_object.h"
#include "core/fpdfapi/parser/cpdf_reference.h"
#include "core/fxcrt/compiler_specific.h"
#include "core/fxcrt/retain_ptr.h"
#include "core/fxcrt/span.h"
#include "fpdfsdk/cpdfsdk_filewriteadapter.h"
#include "fpdfsdk/cpdfsdk_helpers.h"
#include "public/fpdf_save.h"
#include "public/fpdfview.h"

namespace {

struct MemoryFileWrite : public FPDF_FILEWRITE {
  std::vector<uint8_t> data;
};

int WriteToMemory(FPDF_FILEWRITE* self, const void* data, unsigned long size) {
  auto* sink = static_cast<MemoryFileWrite*>(self);
  // SAFETY: CPDFSDK_FileWriteAdapter passes a buffer of exactly |size| bytes.
  auto bytes = UNSAFE_BUFFERS(
      pdfium::span(static_cast<const uint8_t*>(data), static_cast<size_t>(size)));
  sink->data.insert(sink->data.end(), bytes.begin(), bytes.end());
  return 1;
}

// An update section never starts with a file header and always ends with the
// end-of-file marker written by CPDF_Creator.
bool LooksLikeUpdateSection(const std::vector<uint8_t>& data) {
  constexpr char kHeader[] = "%PDF-";
  constexpr char kEnd[] = "%%EOF\r\n";
  constexpr size_t kHeaderLen = sizeof(kHeader) - 1;
  constexpr size_t kEndLen = sizeof(kEnd) - 1;
  if (data.size() <= kHeaderLen + kEndLen) {
    return false;
  }
  pdfium::span<const uint8_t> view(data);
  // SAFETY: both spans hold at least the compared number of bytes.
  if (UNSAFE_BUFFERS(memcmp(view.first(kHeaderLen).data(), kHeader,
                            kHeaderLen)) == 0) {
    return false;
  }
  return UNSAFE_BUFFERS(memcmp(view.last(kEndLen).data(), kEnd, kEndLen)) ==
         0;
}

}  // namespace

extern "C" {

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
FPDF_SaveIncrementalTail(FPDF_DOCUMENT document,
                         FPDF_FILEWRITE* file_write,
                         const uint32_t* extra_obj_nums,
                         unsigned long count) {
  CPDF_Document* doc = CPDFDocumentFromFPDFDocument(document);
  if (!doc || !file_write || !file_write->WriteBlock) {
    return false;
  }
  if (count > 0 && !extra_obj_nums) {
    return false;
  }

  std::vector<uint32_t> extra;
  if (count > 0) {
    // SAFETY: required from caller.
    auto nums = UNSAFE_BUFFERS(
        pdfium::span(extra_obj_nums, static_cast<size_t>(count)));
    extra.assign(nums.begin(), nums.end());
  }

  MemoryFileWrite sink;
  sink.version = 1;
  sink.WriteBlock = &WriteToMemory;
  bool created = false;
  {
    CPDF_Creator creator(doc,
                         pdfium::MakeRetain<CPDFSDK_FileWriteAdapter>(&sink));
    created = creator.CreateIncrementalTail(extra);
  }  // ~CPDF_Creator flushes its archive buffer into |sink|.
  if (!created || !LooksLikeUpdateSection(sink.data)) {
    return false;
  }
  return file_write->WriteBlock(file_write, sink.data.data(),
                                static_cast<unsigned long>(sink.data.size())) !=
         0;
}

FPDF_EXPORT unsigned int FPDF_CALLCONV FPDFPage_GetObjNum(FPDF_PAGE page) {
  CPDF_Page* pdf_page = CPDFPageFromFPDFPage(page);
  if (!pdf_page) {
    return 0;
  }
  RetainPtr<const CPDF_Dictionary> dict = pdf_page->GetDict();
  return dict ? dict->GetObjNum() : 0;
}

// Returns the object number when the value under |key| in the page dictionary
// is an indirect reference, 0 otherwise (absent or direct value).
FPDF_EXPORT unsigned int FPDF_CALLCONV
FPDFPage_GetIndirectObjNumForKey(FPDF_PAGE page, FPDF_BYTESTRING key) {
  CPDF_Page* pdf_page = CPDFPageFromFPDFPage(page);
  if (!pdf_page || !key) {
    return 0;
  }
  RetainPtr<const CPDF_Dictionary> dict = pdf_page->GetDict();
  if (!dict) {
    return 0;
  }
  RetainPtr<const CPDF_Object> value = dict->GetObjectFor(key);
  const CPDF_Reference* ref = value ? value->AsReference() : nullptr;
  return ref ? ref->GetRefObjNum() : 0;
}

}  // extern "C"
