// SZBLEngine 实现
//
// Distributed under the BSD License.
#import "Engine.h"

#include <cstring>

#include <naive_pinyin/c_api.h>

@implementation SZBLEngine {
  void* _ctx;
  BOOL _dirty;
}

- (instancetype)initWithSupportDir:(NSURL*)supportDir {
  self = [super init];
  if (self) {
    _supportDir = supportDir;
    [[NSFileManager defaultManager]
        createDirectoryAtURL:supportDir
withIntermediateDirectories:YES
                 attributes:nil
                      error:nil];
    [self reload];
  }
  return self;
}

- (void)dealloc {
  if (_ctx) np_destroy(_ctx);
}

- (NSURL*)configURL {
  return [_supportDir URLByAppendingPathComponent:@"config.json"];
}

- (NSURL*)userFreqURL {
  return [_supportDir URLByAppendingPathComponent:@"user_freq.json"];
}

- (BOOL)loadDictData:(NSData*)data {
  if (!_ctx || !data.length) return NO;
  return np_load_dict(_ctx, (const char*)data.bytes, (int)data.length) != 0;
}

- (void)reload {
  if (_ctx) {
    np_destroy(_ctx);
    _ctx = NULL;
  }

  // 组装配置：用户 config.json（可选），叠加持久化的 user_freq。
  NSMutableDictionary* cfg = [NSMutableDictionary dictionary];
  NSData* cfgData = [NSData dataWithContentsOfURL:[self configURL]];
  if (cfgData) {
    id obj = [NSJSONSerialization JSONObjectWithData:cfgData
                                             options:0
                                               error:nil];
    if ([obj isKindOfClass:[NSDictionary class]]) {
      [cfg addEntriesFromDictionary:obj];
    } else {
      NSLog(@"Shouzibilu: ignoring invalid config.json");
    }
  }
  if (!cfg[@"max_candidates"]) {
    cfg[@"max_candidates"] = @50;  // 桌面端多给一些候选，配合候选窗滚动
  }
  NSData* freqData = [NSData dataWithContentsOfURL:[self userFreqURL]];
  if (freqData) {
    id obj = [NSJSONSerialization JSONObjectWithData:freqData
                                             options:0
                                               error:nil];
    if ([obj isKindOfClass:[NSArray class]]) {
      NSMutableArray* uf =
          [cfg[@"user_freq"] isKindOfClass:[NSArray class]]
              ? [cfg[@"user_freq"] mutableCopy]
              : [NSMutableArray array];
      [uf addObjectsFromArray:obj];
      cfg[@"user_freq"] = uf;
    }
  }

  NSData* cfgJson = [NSJSONSerialization dataWithJSONObject:cfg
                                                    options:0
                                                      error:nil];
  NSString* cfgStr = [[NSString alloc] initWithData:cfgJson
                                           encoding:NSUTF8StringEncoding];
  _ctx = np_create(cfgStr.UTF8String);
  if (!_ctx) {
    NSLog(@"Shouzibilu: np_create failed, config=%@", cfgStr);
    return;
  }

  // 内置词表（打进 .app/Resources）。
  NSURL* builtin =
      [[NSBundle mainBundle] URLForResource:@"naive_pinyin.dict"
                              withExtension:@"txt"];
  if (![self loadDictData:[NSData dataWithContentsOfURL:builtin]]) {
    NSLog(@"Shouzibilu: failed to load builtin dict at %@", builtin.path);
  }

  // 用户词表：用户目录下全部 *.dict.txt（与内置词表同格式，后加载可提频）。
  NSArray* files = [[NSFileManager defaultManager]
      contentsOfDirectoryAtURL:_supportDir
    includingPropertiesForKeys:nil
                       options:NSDirectoryEnumerationSkipsHiddenFiles
                         error:nil];
  for (NSURL* file in files) {
    if ([file.pathExtension isEqualToString:@"txt"] &&
        [file.lastPathComponent hasSuffix:@".dict.txt"]) {
      if (![self loadDictData:[NSData dataWithContentsOfURL:file]]) {
        NSLog(@"Shouzibilu: failed to load user dict %@", file.path);
      }
    }
  }
  _dirty = NO;
}

- (NSArray<NSDictionary*>*)candidatesForInput:(NSString*)input {
  if (!_ctx || input.length == 0) return @[];
  const char* raw = np_query(_ctx, input.UTF8String);
  if (!raw) return @[];
  NSData* data = [NSData dataWithBytes:raw length:strlen(raw)];
  id obj = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
  if (![obj isKindOfClass:[NSDictionary class]] || obj[@"error"]) return @[];
  id candidates = obj[@"candidates"];
  return [candidates isKindOfClass:[NSArray class]] ? candidates : @[];
}

- (void)commitSegments:(NSArray<NSDictionary*>*)segments {
  if (!_ctx || ![segments isKindOfClass:[NSArray class]] ||
      segments.count == 0) {
    return;
  }
  NSData* json = [NSJSONSerialization dataWithJSONObject:segments
                                                 options:0
                                                   error:nil];
  if (!json) return;
  NSString* str = [[NSString alloc] initWithData:json
                                        encoding:NSUTF8StringEncoding];
  np_commit(_ctx, str.UTF8String);
  _dirty = YES;
}

- (void)saveUserIfNeeded {
  if (!_ctx || !_dirty) return;
  const char* raw = np_dump_user(_ctx);
  if (!raw) return;
  NSString* str = [NSString stringWithUTF8String:raw];
  [str writeToURL:[self userFreqURL] atomically:YES encoding:NSUTF8StringEncoding error:nil];
  _dirty = NO;
}

@end
