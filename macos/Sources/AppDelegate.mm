// SZBAppDelegate 实现
//
// Distributed under the BSD License.
#import "AppDelegate.h"

#import "Engine.h"

@implementation SZBAppDelegate

+ (NSURL*)supportDir {
  NSURL* appSupport = [[NSFileManager defaultManager]
          URLForDirectory:NSApplicationSupportDirectory
                 inDomain:NSUserDomainMask
        appropriateForURL:nil
                   create:YES
                    error:nil];
  return [appSupport URLByAppendingPathComponent:@"Shouzibilu"
                                     isDirectory:YES];
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  NSURL* supportDir = [SZBAppDelegate supportDir];
  _engine = [[SZBLEngine alloc] initWithSupportDir:supportDir];

  // 定期回写用户调频叠加层（commit 只打脏标记）。
  __weak SZBLEngine* engine = _engine;
  [NSTimer scheduledTimerWithTimeInterval:15
                                 repeats:YES
                                   block:^(NSTimer*) {
                                     [engine saveUserIfNeeded];
                                   }];
  NSLog(@"Shouzibilu: ready, support dir %@", supportDir.path);
}

- (void)applicationWillTerminate:(NSNotification*)notification {
  [_engine saveUserIfNeeded];
}

@end
