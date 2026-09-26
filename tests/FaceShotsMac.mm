#include "FaceShotsMac.h"

#import <Cocoa/Cocoa.h>
#import <ImageIO/ImageIO.h>
#import <WebKit/WebKit.h>


namespace ee::shots
{
namespace
{
WKWebView* findIn (NSView* view)
{
    if ([view isKindOfClass:[WKWebView class]])
        return (WKWebView*)view;

    for (NSView* child in view.subviews)
        if (auto* found = findIn (child))
            return found;

    return nil;
}

bool writePng (CGImageRef image, const std::string& path)
{
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    CGImageDestinationRef dest = CGImageDestinationCreateWithURL ((__bridge CFURLRef)url, CFSTR ("public.png"), 1, nullptr);

    if (dest == nullptr)
        return false;

    CGImageDestinationAddImage (dest, image, nullptr);
    const bool ok = CGImageDestinationFinalize (dest);
    CFRelease (dest);
    return ok;
}
} // namespace

void becomeForegroundApp()
{
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp activateIgnoringOtherApps:YES];
}

void pump (int milliseconds)
{
    NSDate* until = [NSDate dateWithTimeIntervalSinceNow:milliseconds / 1000.0];

    while ([until timeIntervalSinceNow] > 0)
    {
        @autoreleasepool
        {
            NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                untilDate:[NSDate dateWithTimeIntervalSinceNow:0.005]
                                                   inMode:NSDefaultRunLoopMode
                                                  dequeue:YES];
            if (event != nil)
                [NSApp sendEvent:event];
        }
    }
}

void* findWebView (void* nsView)
{
    WKWebView* web = findIn ((__bridge NSView*)nsView);

    if (web == nil)
        return nullptr;

    // Not a public property on macOS, but the one every WKWebView-with-alpha
    // answer uses; without it WebKit fills whatever the page leaves clear.
    [web setValue:@NO forKey:@"drawsBackground"];

    if (@available (macOS 12.0, *))
        web.underPageBackgroundColor = NSColor.clearColor;

    return (__bridge void*)web;
}

void evaluate (void* webView, const std::string& script,
               std::function<void (const std::string&, const std::string&)> done)
{
    WKWebView* web = (__bridge WKWebView*)webView;
    NSString* js = [NSString stringWithUTF8String:script.c_str()];

    [web evaluateJavaScript:js
          completionHandler:^(id result, NSError* error) {
              if (error != nil)
              {
                  // WebKit puts the exception's own message in the userInfo,
                  // and a generic "A JavaScript exception occurred" in the
                  // description.
                  NSString* message = error.userInfo[@"WKJavaScriptExceptionMessage"];
                  done ("", (message != nil ? message : error.localizedDescription).UTF8String);
                  return;
              }

              if (result == nil || result == [NSNull null])
                  done ("", "");
              else if ([result isKindOfClass:[NSString class]])
                  done (((NSString*)result).UTF8String, "");
              else
                  done ([result description].UTF8String, "");
          }];
}

void snapshot (void* webView, double pixelsPerCssPixel, std::vector<Crop> crops,
               std::function<void (const std::string&)> done)
{
    WKWebView* web = (__bridge WKWebView*)webView;
    const CGFloat backing = web.window != nil ? web.window.backingScaleFactor : 2.0;
    const CGRect bounds = web.bounds;

    WKSnapshotConfiguration* config = [[WKSnapshotConfiguration alloc] init];
    config.rect = bounds;
    config.afterScreenUpdates = YES;

    // snapshotWidth is in points and the image comes back at the window's
    // backing scale on top of it, so asking for N image pixels per CSS pixel
    // means asking for N / backing points per point.
    config.snapshotWidth = @(bounds.size.width * pixelsPerCssPixel / backing);

    [web takeSnapshotWithConfiguration:config
                     completionHandler:^(NSImage* image, NSError* error) {
                         if (image == nil)
                         {
                             done (error != nil ? error.localizedDescription.UTF8String : "no snapshot");
                             return;
                         }

                         CGImageRef full = [image CGImageForProposedRect:nil context:nil hints:nil];
                         const CGRect whole = CGRectMake (0, 0, CGImageGetWidth (full), CGImageGetHeight (full));
                         const double scale = whole.size.width / bounds.size.width; // image px per CSS px
                         std::string failure;

                         for (const auto& crop : crops)
                         {
                             CGRect area = whole;

                             // Both counted from the top-left: the CSS rect and
                             // CGImageCreateWithImageInRect's.
                             if (crop.rect.w > 0)
                             {
                                 const double m = crop.margin;
                                 area = CGRectIntegral (CGRectIntersection (
                                     whole, CGRectMake ((crop.rect.x - m) * scale, (crop.rect.y - m) * scale,
                                                        (crop.rect.w + 2 * m) * scale, (crop.rect.h + 2 * m) * scale)));
                             }

                             CGImageRef part = CGImageCreateWithImageInRect (full, area);

                             if (part == nullptr || ! writePng (part, crop.path))
                                 failure = "could not write " + crop.path;

                             if (part != nullptr)
                                 CGImageRelease (part);
                         }

                         done (failure);
                     }];
}
} // namespace ee::shots
