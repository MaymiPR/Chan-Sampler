// macOS clipboard for SampleExchange.cpp: the sample as a file URL and as WAV
// data on the general pasteboard.
#import <AppKit/AppKit.h>

#include <string>
#include <vector>

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
        if (path != nullptr && path[0] != 0)
            [item setString: [[NSURL fileURLWithPath: [NSString stringWithUTF8String: path]] absoluteString]
                    forType: NSPasteboardTypeFileURL];
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
        if (urls.count > 0)
        {
            path = [[(NSURL*) urls[0] path] UTF8String];
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
