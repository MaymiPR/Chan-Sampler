// macOS clipboard for SampleExchange.cpp: the sample as a file URL and as WAV
// data on the general pasteboard.
#import <AppKit/AppKit.h>

#include "SampleExchangeMac.h"

namespace exchange
{
static NSString* const wavType = @"com.microsoft.waveform-audio";
static NSString* const aiffType = @"public.aiff-audio";

bool macCopyToClipboard (const char* path, const void* wav, size_t size)
{
    @autoreleasepool
    {
        NSPasteboard* pb = [NSPasteboard generalPasteboard];
        [pb clearContents];
        NSPasteboardItem* item = [[NSPasteboardItem alloc] init];
        NSString* file = path != nullptr && path[0] != 0 ? [NSString stringWithUTF8String: path] : nil;
        NSString* url = file != nil ? [[NSURL fileURLWithPath: file] absoluteString] : nil;
        if (url != nil)
            [item setString: url forType: NSPasteboardTypeFileURL];
        if (wav != nullptr && size > 0)
            [item setData: [NSData dataWithBytes: wav length: size] forType: wavType];
        const BOOL ok = [pb writeObjects: @[ item ]];
       #if ! __has_feature(objc_arc)
        [item release];
       #endif
        return ok;
    }
}

bool macClipboardHasAudio()
{
    @autoreleasepool
    {
        NSPasteboard* pb = [NSPasteboard generalPasteboard];
        if ([pb canReadObjectForClasses: @[ [NSURL class] ]
                                options: @{ NSPasteboardURLReadingFileURLsOnlyKey: @YES }])
            return true;
        return [pb availableTypeFromArray: @[ wavType, aiffType ]] != nil;
    }
}

bool macPasteFromClipboard (std::string& path, std::vector<char>& data, std::string& extension)
{
    @autoreleasepool
    {
        NSPasteboard* pb = [NSPasteboard generalPasteboard];
        NSArray* urls = [pb readObjectsForClasses: @[ [NSURL class] ]
                                          options: @{ NSPasteboardURLReadingFileURLsOnlyKey: @YES }];
        const char* file = urls.count > 0 ? [[(NSURL*) urls[0] path] UTF8String] : nullptr;
        if (file != nullptr)
        {
            path = file;
            return true;
        }
        for (NSString* type in @[ wavType, aiffType ])
        {
            if (NSData* d = [pb dataForType: type])
            {
                const auto* bytes = static_cast<const char*> (d.bytes);
                data.assign (bytes, bytes + d.length);
                extension = type == wavType ? ".wav" : ".aif";
                return true;
            }
        }
        return false;
    }
}
}
