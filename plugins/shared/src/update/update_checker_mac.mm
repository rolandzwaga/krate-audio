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

        // Blocking fetch on the calling worker thread: an NSURLSession data task plus a
        // semaphore (NSURLConnection's sendSynchronousRequest is deprecated since 10.11).
        // The session's request and resource timeouts guarantee the handler runs, so the
        // wait cannot outlive `body`.
        NSURLSessionConfiguration* config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        config.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
        config.timeoutIntervalForRequest = 10.0;
        config.timeoutIntervalForResource = 15.0;
        NSURLSession* session = [NSURLSession sessionWithConfiguration:config];

        std::string body;
        std::string* const bodyOut = &body;
        __block bool ok = false;
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        NSURLSessionDataTask* task = [session dataTaskWithRequest:request
            completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
                if (error == nil && data != nil &&
                    [response isKindOfClass:[NSHTTPURLResponse class]] &&
                    ((NSHTTPURLResponse*)response).statusCode == 200) {
                    bodyOut->assign(static_cast<const char*>(data.bytes), data.length);
                    ok = true;
                }
                dispatch_semaphore_signal(done);
            }];
        [task resume];
        dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
        [session finishTasksAndInvalidate];
#if !__has_feature(objc_arc)
        dispatch_release(done);
#endif

        return ok ? body : std::string{};
    }
}

} // namespace Krate::Plugins

#endif // __APPLE__
