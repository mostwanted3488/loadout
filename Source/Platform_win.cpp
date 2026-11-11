#include "Platform.h"

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>

void Platform::captureScreen (juce::Rectangle<int> area, std::function<void (juce::Image)> cb)
{
    juce::Image result;
    const auto phys = juce::Desktop::getInstance().getDisplays().logicalToPhysical (area);
    const int w = phys.getWidth(), h = phys.getHeight();

    if (w > 0 && h > 0)
    {
        HDC screen = GetDC (nullptr);
        HDC mem = CreateCompatibleDC (screen);

        BITMAPINFO bi {};
        bi.bmiHeader.biSize        = sizeof (BITMAPINFOHEADER);
        bi.bmiHeader.biWidth       = w;
        bi.bmiHeader.biHeight      = -h;   // top-down rows
        bi.bmiHeader.biPlanes      = 1;
        bi.bmiHeader.biBitCount    = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection (screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);

        if (bmp != nullptr && bits != nullptr)
        {
            HGDIOBJ old = SelectObject (mem, bmp);
            BitBlt (mem, 0, 0, w, h, screen, phys.getX(), phys.getY(), SRCCOPY | CAPTUREBLT);
            GdiFlush();

            result = juce::Image (juce::Image::ARGB, w, h, false, juce::SoftwareImageType());
            juce::Image::BitmapData bd (result, juce::Image::BitmapData::writeOnly);

            for (int y = 0; y < h; ++y)
            {
                auto* dst = bd.getLinePointer (y);
                std::memcpy (dst, static_cast<const juce::uint8*> (bits) + (size_t) y * (size_t) w * 4, (size_t) w * 4);
                for (int x = 0; x < w; ++x)
                    dst[x * 4 + 3] = 255;
            }

            SelectObject (mem, old);
        }

        if (bmp != nullptr) DeleteObject (bmp);
        DeleteDC (mem);
        ReleaseDC (nullptr, screen);
    }

    juce::MessageManager::callAsync ([cb, result] { cb (result); });
}

void Platform::playWav (int, const juce::MemoryBlock& wav)
{
    if (wav.getSize() > 0)
        PlaySoundW (reinterpret_cast<LPCWSTR> (wav.getData()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}
