// SZBAppDelegate - 持有引擎单例、中英文状态、输入法菜单动作
//
// Distributed under the BSD License.
#import <Cocoa/Cocoa.h>

@class SZBLEngine;

NS_ASSUME_NONNULL_BEGIN

@interface SZBAppDelegate : NSObject <NSApplicationDelegate>

@property(nonatomic, readonly) SZBLEngine* engine;

// 全局英文模式（Shift 单击或菜单切换）。
@property(nonatomic) BOOL asciiMode;

// 用户目录：~/Library/Application Support/Shouzibilu
+ (NSURL*)supportDir;

- (void)toggleASCIIMode:(id)sender;
- (void)reloadEngine:(id)sender;
- (void)openSupportDir:(id)sender;

@end

NS_ASSUME_NONNULL_END
