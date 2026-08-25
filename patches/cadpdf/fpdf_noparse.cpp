// CadPDF additive patch: page loading without content stream parsing.
//
// Rationale: FPDF_LoadPage() ends with CPDF_Page::ParseContent(), which builds
// the full display list of the page. Reading /Annots does not need it. On A0
// CAD drawings ParseContent() costs about 24 ms per page.
//
// CONTRACT - violating either item corrupts documents:
//   * Never render a page returned by this function: it has no page objects.
//   * Never call FPDFPage_GenerateContent() on it: it would rewrite /Contents
//     from an empty object list and destroy the drawing.
// Intended use: annotation reading only. Close with FPDF_ClosePage().
//
// Not XFA-aware by design; CadPDF builds PDFium without XFA.

#include <utility>

#include "core/fpdfapi/page/cpdf_page.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fxcrt/retain_ptr.h"
#include "fpdfsdk/cpdfsdk_helpers.h"
#include "public/fpdfview.h"

extern "C" {

FPDF_EXPORT FPDF_PAGE FPDF_CALLCONV
FPDF_LoadPageNoParse(FPDF_DOCUMENT document, int page_index) {
  auto* doc = CPDFDocumentFromFPDFDocument(document);
  if (!doc) {
    return nullptr;
  }
  if (page_index < 0 || page_index >= FPDF_GetPageCount(document)) {
    return nullptr;
  }
  RetainPtr<CPDF_Dictionary> dict = doc->GetMutablePageDictionary(page_index);
  if (!CPDF_Page::IsValidPageDictLoose(dict)) {
    return nullptr;
  }
  auto pPage = pdfium::MakeRetain<CPDF_Page>(doc, std::move(dict));
  pPage->AddPageImageCache();
  return FPDFPageFromIPDFPage(pPage.Leak());
}

}  // extern "C"
