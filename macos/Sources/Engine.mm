// SZBLEngine 实现
//
// Distributed under the BSD License.
#import "Engine.h"

#include <cstring>

#include <naive_pinyin/c_api.h>

@implementation SZBLEngine {
  void* _ctx;
  BOOL _dirty;
  NSDictionary<NSString*, NSString*>* _mappings;
  NSDictionary* _cfg;  // 最近一次 reload 的合并配置
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

- (NSURL*)mappingsURL {
  return [_supportDir URLByAppendingPathComponent:@"mappings.json"];
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
    cfg[@"max_candidates"] = @200;  // 桌面端多给候选，配合自绘候选窗翻页
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

  _cfg = [cfg copy];
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
  [self loadMappings];
  _dirty = NO;
}

// mappings.json：用户目录没有就从 bundle 内置默认表拷一份（可手动改）。
- (void)loadMappings {
  NSURL* url = [self mappingsURL];
  if (![[NSFileManager defaultManager] fileExistsAtPath:url.path]) {
    NSURL* builtin =
        [[NSBundle mainBundle] URLForResource:@"mappings" withExtension:@"json"];
    if (builtin) {
      [[NSFileManager defaultManager] copyItemAtURL:builtin
                                              toURL:url
                                              error:nil];
    }
  }
  NSMutableDictionary* result = [NSMutableDictionary dictionary];
  NSData* data = [NSData dataWithContentsOfURL:url];
  if (data) {
    id obj = [NSJSONSerialization JSONObjectWithData:data
                                             options:0
                                               error:nil];
    if ([obj isKindOfClass:[NSDictionary class]]) {
      for (NSString* key in obj) {
        id value = obj[key];
        if (![value isKindOfClass:[NSString class]]) continue;
        NSString* text = [self evalMapping:value forKey:key];
        if (text) result[key] = text;
      }
    } else {
      NSLog(@"Shouzibilu: ignoring invalid mappings.json");
    }
  }
  _mappings = [result copy];
}

// "eval:xxx" 内置求值（web 版是 JS eval，这里只支持固定的四项）；
// 静态字符串原样返回。不认识的 eval 返回 nil（忽略该项）。
- (NSString*)evalMapping:(NSString*)value forKey:(NSString*)key {
  if (![value hasPrefix:@"eval:"]) return value;
  NSString* what = [value substringFromIndex:5];
  NSDateFormatter* fmt = [[NSDateFormatter alloc] init];
  fmt.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
  if ([what isEqualToString:@"date"]) {
    fmt.dateFormat = @"yyyy-MM-dd";
    return [fmt stringFromDate:[NSDate date]];
  }
  if ([what isEqualToString:@"time"]) {
    fmt.dateFormat = @"HH:mm:ss";
    return [fmt stringFromDate:[NSDate date]];
  }
  if ([what isEqualToString:@"datetime"]) {
    fmt.dateFormat = @"yyyy-MM-dd HH:mm:ss";
    return [fmt stringFromDate:[NSDate date]];
  }
  if ([what isEqualToString:@"uuid"]) {
    return [[NSUUID UUID] UUIDString].lowercaseString;
  }
  NSLog(@"Shouzibilu: unsupported mapping %@ -> %@, skipped", key, value);
  return nil;
}

- (NSDictionary<NSString*, NSString*>*)mappings {
  return _mappings ? _mappings : @{};
}

- (BOOL)dynamicCommaEnabled {
  id v = _cfg[@"dynamic_comma"];
  return [v respondsToSelector:@selector(boolValue)] && [v boolValue];
}

- (NSArray<NSNumber*>*)segmentBoundariesForInput:(NSString*)input {
  if (!_ctx || input.length == 0) return @[];
  const char* raw = np_segment(_ctx, input.UTF8String);
  if (!raw) return @[];
  NSData* data = [NSData dataWithBytes:raw length:strlen(raw)];
  id obj = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
  if (![obj isKindOfClass:[NSDictionary class]] || obj[@"error"]) return @[];
  id boundaries = obj[@"boundaries"];
  return [boundaries isKindOfClass:[NSArray class]] ? boundaries : @[];
}

- (void)learnWord:(NSString*)word key:(NSString*)key {
  if (!_ctx || word.length == 0 || key.length == 0) return;
  np_learn_word(_ctx, key.UTF8String, word.UTF8String);
  _dirty = YES;
}

- (NSURL*)exportUserFreq {
  if (!_ctx) return nil;
  const char* raw = np_dump_user(_ctx);
  if (!raw) return nil;
  NSData* data = [NSData dataWithBytes:raw length:strlen(raw)];
  id obj = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
  if (![obj isKindOfClass:[NSArray class]]) return nil;
  NSArray* entries = obj;
  entries = [entries
      sortedArrayUsingComparator:^NSComparisonResult(NSDictionary* a,
                                                     NSDictionary* b) {
        return [b[@"count"] compare:a[@"count"]];  // count 降序
      }];
  NSMutableString* tsv = [NSMutableString stringWithString:@"count\tword\tpinyin\n"];
  for (NSDictionary* e in entries) {
    [tsv appendFormat:@"%@\t%@\t%@\n", e[@"count"], e[@"word"], e[@"pinyin"]];
  }
  NSURL* dir = [[self supportDir] URLByAppendingPathComponent:@"exports"
                                                  isDirectory:YES];
  [[NSFileManager defaultManager] createDirectoryAtURL:dir
                           withIntermediateDirectories:YES
                                            attributes:nil
                                                 error:nil];
  NSDateFormatter* fmt = [[NSDateFormatter alloc] init];
  fmt.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
  fmt.dateFormat = @"yyyyMMdd-HHmmss";
  NSURL* file = [dir
      URLByAppendingPathComponent:[NSString stringWithFormat:
                                                @"user_freq_%@.tsv",
                                                [fmt stringFromDate:[NSDate date]]]];
  if (![tsv writeToURL:file atomically:YES encoding:NSUTF8StringEncoding error:nil]) {
    return nil;
  }
  return file;
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
