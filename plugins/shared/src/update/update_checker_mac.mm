#include "update_checker.h"

#ifdef __APPLE__

#import <Foundation/Foundation.h>

namespace Krate::Plugins {

std::string UpdateChecker::fetchJsonMac(const std::string& url) {
    @autoreleasepool {
        NSString* urlString = [NSString stringWithUTF8String:url.c_str()];
        NSURL* nsUrl = [NSURL URLWithString:urlString];
        if (!nsUrl) return {};

        NSURLRequest* request = [NSURLRequest requestWithURL:nsUrl
            cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
            timeoutInterval:10.0];

        // Synchronous request (called from worker thread)
        NSHTTPURLResponse* response = nil;
        NSError* error = nil;
        NSData* data = [NSURLConnection sendSynchronousRequest:request
            returningResponse:&response error:&error];

        if (error || !data || response.statusCode != 200) {
            return {};
        }

        return std::string(static_cast<const char*>(data.bytes), data.length);
    }
}

} // namespace Krate::Plugins

#endif // __APPLE__
