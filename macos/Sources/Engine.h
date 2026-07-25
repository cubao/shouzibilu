// SZBLEngine - naive_pinyin C++ 核心的 ObjC 封装
//
// Distributed under the BSD License.
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

// 进程内单实例（由 AppDelegate 持有，所有 InputController 共享）。
// 所有方法须在主线程调用（np_query/np_dump_user 返回内部静态缓冲区）。
@interface SZBLEngine : NSObject

// 用户目录：~/Library/Application Support/Shouzibilu
//   config.json       可选，核心配置（同 npm 包格式：
//                     {"shuangpin":{"map":{...}}, "fuzzy":[["z","zh"]], ...}）
//   *.dict.txt        可选，追加词表（与 data/naive_pinyin.dict.txt 同格式）
//   user_freq.json    自动维护的动态调频叠加层（np_dump_user 导出）
//   mappings.json     可选，动态词映射表（同 web 版 cfg.mappings：
//                     {",check": "✅", ...}；eval:date/time/datetime/uuid
//                     内置求值，其它 eval: 项忽略）。不存在时从 app 内
//                     置默认表拷贝一份，方便用户手动改。
@property(nonatomic, readonly) NSURL* supportDir;

- (instancetype)initWithSupportDir:(NSURL*)supportDir;

// 查询候选。返回 [{@"text": NSString, @"consumed": NSNumber,
//                  @"score": NSNumber,
//                  @"segments": @[@{@"key":.., @"word":..}]}]
// 输入非法或无候选时返回空数组。
- (NSArray<NSDictionary*>*)candidatesForInput:(NSString*)input;

// 动态调频：segments 为候选 JSON 的 segments 字段。
- (void)commitSegments:(NSArray<NSDictionary*>*)segments;

// 学习自造词：key 为空格分隔音节（如 "tang zhi xiong"）。
- (void)learnWord:(NSString*)word key:(NSString*)key;

// 动态词映射表（"," 开头触发，同 web 版语义）。
- (NSDictionary<NSString*, NSString*>*)mappings;

// 将用户调频叠加层写回 user_freq.json（仅在有变更时真正写盘）。
- (void)saveUserIfNeeded;

// 重建引擎并重载全部词典（配置/用户词表变更后调用）。
- (void)reload;

@end

NS_ASSUME_NONNULL_END
