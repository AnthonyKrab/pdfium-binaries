// CadPDF additive patch: page rendering with a filtered set of annotations.
//
// FPDF_RenderPageBitmapWithMatrixFiltered() renders the page content exactly
// as FPDF_RenderPageBitmapWithMatrix() does, then the page's annotations on
// top of it in the same pass, so blend modes (/BM /Multiply) and transparency
// groups inside appearance streams see the real page as backdrop.
//
// It differs from FPDF_RenderPageBitmapWithMatrix(..., FPDF_ANNOT):
//   * The document is never modified. CPDF_AnnotList is not used: its
//     constructor converts every annotation into an indirect object
//     (ConvertToIndirectObjectAt) and creates synthetic popups. A CPDF_Annot
//     is built only for an annotation whose /AP has an /N entry, so
//     CPDF_Annot::GenerateAPIfNeeded() never writes into the dictionary.
//     Annotations without /AP /N are not drawn.
//   * Annotations of the listed subtypes (FPDF_ANNOT_* values) and at the
//     listed indices of the page's /Annots array are skipped. The index is
//     the one FPDFPage_GetAnnot() and FPDFPage_RemoveAnnot() use.
//   * An annotation whose own dictionary carries /OC is skipped when that
//     optional content is off (CPDF_AnnotList does not check it).
//   * Hidden, NoView (Print when FPDF_PRINTING is set) and popups are skipped,
//     as in CPDF_AnnotList::DisplayPass().
//   * Annotations whose rectangle misses the clip are skipped before their
//     appearance stream is parsed.
//   * Widgets are drawn from their appearance streams in a second pass, after
//     every other annotation.
// FPDF_ANNOT in |flags| is ignored: annotations are always drawn.
//
// Lifetime: CPDF_Annot owns the CPDF_Form of its appearance stream, and
// CPDF_RenderContext::AppendLayer() keeps only a raw pointer to it. The
// annotations live in the page render context (annots_) until the render is
// finished and the context is cleared.
//
// Not XFA-aware by design; CadPDF builds PDFium without XFA.

#include <memory>
#include <utility>
#include <vector>

#include "constants/annotation_common.h"
#include "constants/annotation_flags.h"
#include "core/fpdfapi/page/cpdf_occontext.h"
#include "core/fpdfapi/page/cpdf_page.h"
#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/render/cpdf_pagerendercontext.h"
#include "core/fpdfapi/render/cpdf_progressiverenderer.h"
#include "core/fpdfapi/render/cpdf_rendercontext.h"
#include "core/fpdfapi/render/cpdf_renderoptions.h"
#include "core/fpdfdoc/cpdf_annot.h"
#include "core/fxcrt/compiler_specific.h"
#include "core/fxcrt/fx_coordinates.h"
#include "core/fxcrt/retain_ptr.h"
#include "core/fxge/cfx_renderdevice.h"
#include "core/fxge/dib/cfx_dibitmap.h"
#include "fpdfsdk/cpdfsdk_helpers.h"
#include "public/fpdfview.h"

namespace {

// Keeps the annotations (and through them their appearance forms) alive for
// as long as the render context that points at the forms.
class OwnedAnnots final : public CPDF_PageRenderContext::AnnotListIface {
 public:
  OwnedAnnots() = default;
  ~OwnedAnnots() override = default;

  std::vector<std::unique_ptr<CPDF_Annot>> annots;
};

bool Contains(const int* values, int count, int value) {
  if (!values || count <= 0) {
    return false;
  }
  // SAFETY: the caller passes |count| elements.
  UNSAFE_BUFFERS({
    for (int k = 0; k < count; ++k) {
      if (values[k] == value) {
        return true;
      }
    }
  });
  return false;
}

}  // namespace

extern "C" {

FPDF_EXPORT void FPDF_CALLCONV
FPDF_RenderPageBitmapWithMatrixFiltered(FPDF_BITMAP bitmap,
                                        FPDF_PAGE page,
                                        const FS_MATRIX* matrix,
                                        const FS_RECTF* clipping,
                                        int flags,
                                        const int* skip_subtypes,
                                        int skip_subtype_count,
                                        const int* skip_indices,
                                        int skip_index_count) {
  CPDF_Page* pPage = CPDFPageFromFPDFPage(page);
  if (!pPage) {
    return;
  }

  RetainPtr<CFX_DIBitmap> pBitmap(CFXDIBitmapFromFPDFBitmap(bitmap));
  if (!pBitmap) {
    return;
  }
  ValidateBitmapPremultiplyState(pBitmap.Get());

  auto owned_context = std::make_unique<CPDF_PageRenderContext>();
  CPDF_PageRenderContext* context = owned_context.get();
  CPDF_Page::RenderContextClearer clearer(pPage);
  pPage->SetRenderContext(std::move(owned_context));

#if defined(PDF_USE_SKIA)
  CFX_DIBitmap::ScopedPremultiplier scoped_premultiplier(pBitmap);
#endif
  auto device = CFX_RenderDevice::CreateForBitmap(
      std::move(pBitmap), !!(flags & FPDF_REVERSE_BYTE_ORDER));
  if (!device) {
    return;
  }
  context->device_ = std::move(device);

  CFX_FloatRect clipping_rect;
  if (clipping) {
    clipping_rect = CFXFloatRectFromFSRectF(*clipping);
  }
  const FX_RECT clip_rect = clipping_rect.ToFxRect();

  CFX_Matrix transform = pPage->GetDisplayMatrix();
  if (matrix) {
    transform *= CFXMatrixFromFSMatrix(*matrix);
  }

  // Options as in RenderPageImpl() (fpdfsdk/cpdfsdk_renderpage.cpp).
  context->options_ = std::make_unique<CPDF_RenderOptions>();
  auto& options = context->options_->GetOptions();
  options.bClearType = !!(flags & FPDF_LCD_TEXT);
  options.bNoNativeText = !!(flags & FPDF_NO_NATIVETEXT);
  options.bLimitedImageCache = !!(flags & FPDF_RENDER_LIMITEDIMAGECACHE);
  options.bForceHalftone = !!(flags & FPDF_RENDER_FORCEHALFTONE);
  options.bNoTextSmooth = !!(flags & FPDF_RENDER_NO_SMOOTHTEXT);
  options.bNoImageSmooth = !!(flags & FPDF_RENDER_NO_SMOOTHIMAGE);
  options.bNoPathSmooth = !!(flags & FPDF_RENDER_NO_SMOOTHPATH);
  if (flags & FPDF_GRAYSCALE) {
    context->options_->SetColorMode(CPDF_RenderOptions::kGray);
  }
  const bool is_printing = !!(flags & FPDF_PRINTING);
  context->options_->SetOCContext(pdfium::MakeRetain<CPDF_OCContext>(
      pPage->GetDocument(),
      is_printing ? CPDF_OCContext::kPrint : CPDF_OCContext::kView));

  context->device_->SaveState();
  context->device_->SetBaseClip(clip_rect);
  context->device_->SetClip_Rect(clip_rect);
  context->context_ = std::make_unique<CPDF_RenderContext>(
      pPage->GetDocument(), pPage->GetMutablePageResources(),
      pPage->GetPageImageCache());

  context->context_->AppendLayer(pPage, transform);

  auto owned_annots = std::make_unique<OwnedAnnots>();
  OwnedAnnots* annots = owned_annots.get();
  context->annots_ = std::move(owned_annots);

  RetainPtr<CPDF_Array> annots_array = pPage->GetMutableAnnotsArray();
  if (annots_array) {
    // Pass 0: everything but widgets; pass 1: widgets. Order inside a pass is
    // the order of /Annots. The second pass runs only if the first saw one.
    bool has_widgets = false;
    for (int pass = 0; pass < 2; ++pass) {
      const bool widget_pass = pass == 1;
      if (widget_pass && !has_widgets) {
        break;
      }
      for (size_t i = 0; i < annots_array->size(); ++i) {
        RetainPtr<CPDF_Dictionary> dict =
            ToDictionary(annots_array->GetMutableDirectObjectAt(i));
        if (!dict) {
          continue;
        }
        const CPDF_Annot::Subtype subtype = CPDF_Annot::StringToAnnotSubtype(
            dict->GetByteStringFor(pdfium::annotation::kSubtype));
        if ((subtype == CPDF_Annot::Subtype::WIDGET) != widget_pass) {
          has_widgets |= subtype == CPDF_Annot::Subtype::WIDGET;
          continue;
        }
        if (subtype == CPDF_Annot::Subtype::POPUP) {
          continue;
        }
        if (Contains(skip_subtypes, skip_subtype_count,
                     static_cast<int>(subtype)) ||
            Contains(skip_indices, skip_index_count, static_cast<int>(i))) {
          continue;
        }
        // Only annotations with an existing normal appearance: with /AP /N
        // present CPDF_Annot::ShouldGenerateAP() is false, so neither the
        // constructor nor DrawInContext() writes into the document.
        RetainPtr<const CPDF_Dictionary> ap =
            dict->GetDictFor(pdfium::annotation::kAP);
        if (!ap || !ap->GetDictFor("N")) {
          continue;
        }
        const uint32_t annot_flags =
            dict->GetIntegerFor(pdfium::annotation::kF);
        if (annot_flags & pdfium::annotation_flags::kHidden) {
          continue;
        }
        if (is_printing &&
            (annot_flags & pdfium::annotation_flags::kPrint) == 0) {
          continue;
        }
        if (!is_printing &&
            (annot_flags & pdfium::annotation_flags::kNoView)) {
          continue;
        }
        RetainPtr<const CPDF_Dictionary> oc = dict->GetDictFor("OC");
        if (oc && !context->options_->CheckOCGDictVisible(oc.Get())) {
          continue;
        }
        // The constructor reads the dictionary only (no generation, see above)
        // and parses no appearance stream; that happens in DrawInContext().
        auto annot =
            std::make_unique<CPDF_Annot>(std::move(dict), pPage->GetDocument());
        // The rectangle DrawInContext() maps the appearance into
        // (AnnotGetMatrix), not the raw /Rect: for a text markup annotation
        // carrying PDFium's generated-appearance key it is the bounding box
        // of /QuadPoints. One pixel of margin for antialiasing, added in
        // float before the saturating conversion to integers.
        CFX_FloatRect device_float_rect = transform.TransformRect(annot->GetRect());
        device_float_rect.Inflate(1.0f, 1.0f);
        FX_RECT device_rect = device_float_rect.GetOuterRect();
        device_rect.Intersect(clip_rect);
        if (device_rect.IsEmpty()) {
          continue;
        }
        if (annot->DrawInContext(pPage, context->context_.get(), transform,
                                 CPDF_Annot::AppearanceMode::kNormal)) {
          annots->annots.push_back(std::move(annot));
        }
      }
    }
  }

  context->renderer_ = std::make_unique<CPDF_ProgressiveRenderer>(
      context->context_.get(), context->device_.get(), context->options_.get());
  context->renderer_->Start(nullptr);
  context->device_->RestoreState(false);
}

}  // extern "C"
