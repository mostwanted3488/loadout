// Compiled with -fobjc-arc (see CMakeLists.txt)
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#include "Platform.h"

namespace
{
    juce::Image imageFromCGImage (CGImageRef cg)
    {
        const int w = (int) CGImageGetWidth (cg);
        const int h = (int) CGImageGetHeight (cg);
        if (w <= 0 || h <= 0)
            return {};

        juce::Image img (juce::Image::ARGB, w, h, false, juce::SoftwareImageType());
        {
            juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
            CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
            CGContextRef ctx = CGBitmapContextCreate (bd.data, (size_t) w, (size_t) h, 8, (size_t) bd.lineStride, cs,
                                                      (CGBitmapInfo) kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
            CGColorSpaceRelease (cs);

            if (ctx == nullptr)
                return {};

            CGContextDrawImage (ctx, CGRectMake (0, 0, w, h), cg);
            CGContextRelease (ctx);
        }
        return img;
    }

    void deliver (std::function<void (juce::Image)> cb, juce::Image img)
    {
        juce::MessageManager::callAsync ([cb, img] { cb (img); });
    }
}

void Platform::captureScreen (juce::Rectangle<int> area, std::function<void (juce::Image)> cb)
{
    double scale = 2.0;
    if (auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (area))
        scale = d->scale;

    if (@available (macOS 14.0, *))
    {
        if (! CGPreflightScreenCaptureAccess())
            CGRequestScreenCaptureAccess();   // shows the system prompt the first time

        const int ax = area.getX(), ay = area.getY();

        [SCShareableContent getShareableContentWithCompletionHandler: ^(SCShareableContent* content, NSError* error)
        {
            if (content == nil || error != nil || content.displays.count == 0)
            {
                deliver (cb, {});
                return;
            }

            SCDisplay* chosen = content.displays.firstObject;
            for (SCDisplay* d in content.displays)
            {
                const CGRect b = CGDisplayBounds (d.displayID);
                if ((int) b.origin.x == ax && (int) b.origin.y == ay)
                {
                    chosen = d;
                    break;
                }
            }

            SCContentFilter* filter = [[SCContentFilter alloc] initWithDisplay: chosen excludingWindows: @[]];
            SCStreamConfiguration* cfg = [[SCStreamConfiguration alloc] init];
            cfg.width  = (size_t) ((double) chosen.width  * scale);
            cfg.height = (size_t) ((double) chosen.height * scale);
            cfg.showsCursor = NO;

            [SCScreenshotManager captureImageWithFilter: filter
                                          configuration: cfg
                                      completionHandler: ^(CGImageRef image, NSError* captureError)
            {
                if (image == nil || captureError != nil)
                {
                    deliver (cb, {});
                    return;
                }
                deliver (cb, imageFromCGImage (image));
            }];
        }];
        return;
    }

    deliver (cb, {});
}

void Platform::playWav (int index, const juce::MemoryBlock& wav)
{
    static NSSound* cache[32];
    if (index < 0 || index >= 32 || wav.getSize() == 0)
        return;

    if (cache[index] == nil)
    {
        NSData* data = [NSData dataWithBytes: wav.getData() length: wav.getSize()];
        cache[index] = [[NSSound alloc] initWithData: data];
    }

    if (NSSound* s = cache[index])
    {
        [s stop];
        [s play];
    }
}
