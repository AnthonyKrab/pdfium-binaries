// CadPDF additive patch: facts about an annotation the public API does not
// give, read without changing the document.
//
// FPDFAnnot_GetObjNum(): object number of the annotation dictionary, 0 for a
//   direct one.
// FPDFAnnot_GetIndirectObjNumForKey(): object number an indirect reference
//   under |key| points at, 0 otherwise (absent, direct value, not a
//   reference). FPDFAnnot_GetLinkedAnnot() needs /Type /Annot in the target,
//   which is optional (ISO 32000-1, table 164).
// FPDFAnnot_HasNormalAP(): whether GetAnnotAP() finds a normal appearance
//   stream, the condition under which CPDF_Annot draws. FPDFAnnot_GetAP()
//   uses the no-fallback lookup and decodes the stream.
// FPDFAnnot_IsOCVisible(): whether /OC of the annotation dictionary is on, by
//   the check the renderer applies; true without /OC, false for a null
//   handle.
//
// All four only read. Safe on a page from FPDF_LoadPageNoParse(): they touch
// the annotation dictionary and the document catalog, not page content.
//
// Not XFA-aware by design; CadPDF builds PDFium without XFA.

#include "core/fpdfapi/page/cpdf_annotcontext.h"
#include "core/fpdfapi/page/cpdf_occontext.h"
#include "core/fpdfapi/page/ipdf_page.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_object.h"
#include "core/fpdfapi/parser/cpdf_reference.h"
#include "core/fpdfapi/parser/cpdf_stream.h"
#include "core/fpdfdoc/cpdf_annot.h"
#include "core/fxcrt/retain_ptr.h"
#include "fpdfsdk/cpdfsdk_helpers.h"
#include "public/fpdf_annot.h"

extern "C" {

FPDF_EXPORT unsigned int FPDF_CALLCONV
FPDFAnnot_GetObjNum(FPDF_ANNOTATION annot) {
  CPDF_AnnotContext* context = CPDFAnnotContextFromFPDFAnnotation(annot);
  if (!context) {
    return 0;
  }
  return context->GetAnnotDict()->GetObjNum();
}

FPDF_EXPORT unsigned int FPDF_CALLCONV
FPDFAnnot_GetIndirectObjNumForKey(FPDF_ANNOTATION annot, FPDF_BYTESTRING key) {
  CPDF_AnnotContext* context = CPDFAnnotContextFromFPDFAnnotation(annot);
  if (!context || !key) {
    return 0;
  }
  // GetObjectFor(), not GetDirectObjectFor(): the reference itself, without
  // loading its target.
  RetainPtr<const CPDF_Object> value =
      context->GetAnnotDict()->GetObjectFor(key);
  const CPDF_Reference* ref = value ? value->AsReference() : nullptr;
  return ref ? ref->GetRefObjNum() : 0;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
FPDFAnnot_HasNormalAP(FPDF_ANNOTATION annot) {
  CPDF_AnnotContext* context = CPDFAnnotContextFromFPDFAnnotation(annot);
  if (!context) {
    return false;
  }
  RetainPtr<CPDF_Dictionary> dict = context->GetMutableAnnotDict();
  return !!GetAnnotAP(dict.Get(), CPDF_Annot::AppearanceMode::kNormal);
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
FPDFAnnot_IsOCVisible(FPDF_ANNOTATION annot, FPDF_BOOL printing) {
  CPDF_AnnotContext* context = CPDFAnnotContextFromFPDFAnnotation(annot);
  if (!context) {
    return false;
  }
  RetainPtr<const CPDF_Dictionary> oc =
      context->GetAnnotDict()->GetDictFor("OC");
  if (!oc) {
    return true;
  }
  CPDF_Document* doc = context->GetPage()->GetDocument();
  if (!doc) {
    return false;
  }
  auto oc_context = pdfium::MakeRetain<CPDF_OCContext>(
      doc, printing ? CPDF_OCContext::kPrint : CPDF_OCContext::kView);
  return oc_context->CheckOCGDictVisible(oc.Get());
}

}  // extern "C"
