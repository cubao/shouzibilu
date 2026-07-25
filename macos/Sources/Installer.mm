// SZBInstaller 实现
//
// Distributed under the BSD License.
#import "Installer.h"

#import <Carbon/Carbon.h>
#import <Cocoa/Cocoa.h>

static NSString* const kInputModeID = @"im.cubao.inputmethod.Shouzibilu.Hans";

@implementation SZBInstaller

- (TISInputSourceRef)findSource:(NSString*)sourceID {
  NSArray* sources = CFBridgingRelease(TISCreateInputSourceList(NULL, true));
  for (id obj in sources) {
    TISInputSourceRef source = (__bridge TISInputSourceRef)obj;
    // Get 规则的属性值不归我们所有，用 __bridge（不能 CFBridgingRelease，
    // 否则过度释放会崩）。
    NSString* sid = (__bridge NSString*)TISGetInputSourceProperty(
        source, kTISPropertyInputSourceID);
    if ([sid isEqualToString:sourceID]) {
      CFRetain(source);  // 一次性 CLI 进程，容忍泄漏
      return source;
    }
  }
  return NULL;
}

- (BOOL)boolProperty:(CFStringRef)key ofSource:(TISInputSourceRef)source {
  CFBooleanRef value =
      (CFBooleanRef)TISGetInputSourceProperty(source, key);
  return value ? CFBooleanGetValue(value) : NO;
}

- (BOOL)modeEnabled {
  TISInputSourceRef source = [self findSource:kInputModeID];
  return source &&
         [self boolProperty:kTISPropertyInputSourceIsEnabled ofSource:source];
}

- (void)registerInputSource {
  if ([self modeEnabled]) {
    NSLog(@"Shouzibilu: input mode already registered and enabled: %@",
          kInputModeID);
    return;
  }
  NSURL* appURL = [NSURL fileURLWithPath:[[NSBundle mainBundle] bundlePath]];
  TISRegisterInputSource((__bridge CFURLRef)appURL);
  NSLog(@"Shouzibilu: registered input source from %@", appURL.path);
}

- (void)enableInputSource {
  TISInputSourceRef source = [self findSource:kInputModeID];
  if (!source) {
    NSLog(@"Shouzibilu: input mode not found: %@ (run --install first)",
          kInputModeID);
    return;
  }
  if (![self boolProperty:kTISPropertyInputSourceIsEnabled ofSource:source]) {
    OSStatus err = TISEnableInputSource(source);
    NSLog(@"Shouzibilu: enable %@ for %@", err == noErr ? @"succeeds" : @"fails",
          kInputModeID);
  }
}

- (void)selectInputSource {
  TISInputSourceRef source = [self findSource:kInputModeID];
  if (!source) {
    NSLog(@"Shouzibilu: input mode not found: %@", kInputModeID);
    return;
  }
  if (![self boolProperty:kTISPropertyInputSourceIsEnabled ofSource:source]) {
    OSStatus err = TISEnableInputSource(source);
    if (err != noErr) {
      NSLog(@"Shouzibilu: failed to enable %@", kInputModeID);
      return;
    }
  }
  BOOL capable = [self boolProperty:kTISPropertyInputSourceIsSelectCapable
                           ofSource:source];
  BOOL selected = [self boolProperty:kTISPropertyInputSourceIsSelected
                            ofSource:source];
  if (capable && !selected) {
    OSStatus err = TISSelectInputSource(source);
    NSLog(@"Shouzibilu: select %@ for %@ (err=%d)",
          err == noErr ? @"succeeds" : @"fails", kInputModeID, (int)err);
  } else {
    NSLog(@"Shouzibilu: cannot select %@ (capable=%d selected=%d)",
          kInputModeID, capable, selected);
  }
}

- (void)disableInputSource {
  TISInputSourceRef source = [self findSource:kInputModeID];
  if (source &&
      [self boolProperty:kTISPropertyInputSourceIsEnabled ofSource:source]) {
    OSStatus err = TISDisableInputSource(source);
    NSLog(@"Shouzibilu: disable %@ for %@",
          err == noErr ? @"succeeds" : @"fails", kInputModeID);
  }
}

@end
