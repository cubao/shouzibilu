// SZBInstaller - 输入法注册/启用/选中（移植自 Squirrel 的 InputSource.swift）
//
// Distributed under the BSD License.
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface SZBInstaller : NSObject

- (void)registerInputSource;
- (void)enableInputSource;
- (void)selectInputSource;
- (void)disableInputSource;

@end

NS_ASSUME_NONNULL_END
