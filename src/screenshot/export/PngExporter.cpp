#include "PngExporter.h"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")

using Microsoft::WRL::ComPtr;

bool PngExporter::Encode(const std::vector<std::byte>& bgra8, int width, int height, std::vector<std::byte>& outPng) {
    if (bgra8.empty() || width <= 0 || height <= 0) return false;

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        NULL,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory)
    );
    if (FAILED(hr)) return false;

    ComPtr<IStream> stream;
    hr = CreateStreamOnHGlobal(NULL, TRUE, &stream);
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapEncoder> encoder;
    hr = factory->CreateEncoder(GUID_ContainerFormatPng, NULL, &encoder);
    if (FAILED(hr)) return false;

    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    hr = encoder->CreateNewFrame(&frame, &props);
    if (FAILED(hr)) return false;

    hr = frame->Initialize(props.Get());
    if (FAILED(hr)) return false;

    hr = frame->SetSize(width, height);
    if (FAILED(hr)) return false;

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    hr = frame->SetPixelFormat(&format);
    if (FAILED(hr) || format != GUID_WICPixelFormat32bppBGRA) return false;

    UINT stride = width * 4;
    UINT bufferSize = static_cast<UINT>(bgra8.size());
    hr = frame->WritePixels(height, stride, bufferSize, reinterpret_cast<BYTE*>(const_cast<std::byte*>(bgra8.data())));
    if (FAILED(hr)) return false;

    hr = frame->Commit();
    if (FAILED(hr)) return false;

    hr = encoder->Commit();
    if (FAILED(hr)) return false;

    STATSTG statstg;
    hr = stream->Stat(&statstg, STATFLAG_NONAME);
    if (FAILED(hr)) return false;

    SIZE_T exactSize = static_cast<SIZE_T>(statstg.cbSize.QuadPart);

    HGLOBAL hGlobal = NULL;
    hr = GetHGlobalFromStream(stream.Get(), &hGlobal);
    if (FAILED(hr)) return false;

    void* pData = GlobalLock(hGlobal);
    if (pData) {
        outPng.assign(static_cast<std::byte*>(pData), static_cast<std::byte*>(pData) + exactSize);
        GlobalUnlock(hGlobal);
        return true;
    }

    return false;
}
