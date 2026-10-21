#include "core/ocr/ImageOcrProvider.h"

#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Globalization.h>

#include "common/StringUtil.h"
#include "common/Utf8.h"
#include "common/Log.h"

namespace clipd2d::core::ocr {

class WindowsOcrProvider final : public ImageOcrProvider {
 public:
  common::Status Recognize(const ImageOcrRequest& request, ImageOcrResult* result) override {
    if (result == nullptr) {
      return common::Status::Error(common::StatusCode::InvalidArgument, "OCR result cannot be null.");
    }
    if (request.image_path.empty()) {
      return common::Status::Error(common::StatusCode::InvalidArgument, "OCR image path is empty.");
    }

    try {
      // 1. Resolve storage file
      auto file = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(request.image_path).get();
      
      // 2. Open file stream
      auto stream = file.OpenAsync(winrt::Windows::Storage::FileAccessMode::Read).get();
      
      // 3. Decode software bitmap
      auto decoder = winrt::Windows::Graphics::Imaging::BitmapDecoder::CreateAsync(stream).get();
      auto bitmap = decoder.GetSoftwareBitmapAsync().get();

      // 4. Create Windows OCR Engine
      winrt::Windows::Media::Ocr::OcrEngine engine = nullptr;
      if (!request.language_hint.empty()) {
        winrt::Windows::Globalization::Language lang(request.language_hint);
        if (winrt::Windows::Media::Ocr::OcrEngine::IsLanguageSupported(lang)) {
          engine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromLanguage(lang);
        }
      }

      if (!engine) {
        engine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromUserProfileLanguages();
      }

      if (!engine) {
        return common::Status::Error(common::StatusCode::Unavailable, "Windows OCR engine could not be created.");
      }

      // 5. Run OCR recognition
      auto ocr_result = engine.RecognizeAsync(bitmap).get();
      std::wstring text(ocr_result.Text());

      // 6. Populate result
      result->utf8_text = common::WideToUtf8(text).utf8;
      result->engine_name = "Windows OCR";
      result->language_used = common::WideToUtf8(engine.RecognizerLanguage().LanguageTag()).utf8;

      return common::Status::Ok();
    } catch (const winrt::hresult_error& ex) {
      return common::Status::Error(
          common::StatusCode::InternalError,
          "Windows OCR failed with WinRT exception: " + common::WideToUtf8(ex.message()).utf8);
    } catch (const std::exception& ex) {
      return common::Status::Error(
          common::StatusCode::InternalError,
          std::string("Windows OCR failed: ") + ex.what());
    } catch (...) {
      return common::Status::Error(common::StatusCode::InternalError, "Windows OCR failed with an unknown exception.");
    }
  }
};

std::unique_ptr<ImageOcrProvider> CreateWindowsOcrProvider() {
  return std::make_unique<WindowsOcrProvider>();
}

}  // namespace clipd2d::core::ocr
