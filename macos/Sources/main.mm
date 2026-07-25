// Shouzibilu 入口
//
// 无参数：作为输入法服务进程运行（由 TextInputServices 拉起）。
// 带参数：执行一次性管理动作（参考 Squirrel 的 Main.swift）：
//   --install / --register-input-source   注册输入法
//   --enable-input-source                 启用输入模式
//   --select-input-source                 选中输入法
//   --disable-input-source                停用输入模式
//   --quit                                退出所有运行中的实例
//   --help                                打印帮助
//
// Distributed under the BSD License.
#import <Cocoa/Cocoa.h>
#import <InputMethodKit/InputMethodKit.h>

#import "AppDelegate.h"
#import "Engine.h"
#import "Installer.h"

static void PrintHelp() {
  puts(
      "Shouzibilu 手自笔录 - a naive pinyin input method for macOS\n"
      "Usage: Shouzibilu [action]\n"
      "  --install, --register-input-source    register input source\n"
      "  --enable-input-source                 enable input mode\n"
      "  --select-input-source                 select input method\n"
      "  --disable-input-source                disable input mode\n"
      "  --quit                                quit all running instances\n"
      "  --query <input>                       query engine candidates (debug)\n"
      "  --help                                print this message");
}

int main(int /*argc*/, const char* /*argv*/[]) {
  @autoreleasepool {
    NSArray<NSString*>* args = [NSProcessInfo processInfo].arguments;
    if (args.count > 1) {
      SZBInstaller* installer = [[SZBInstaller alloc] init];
      NSString* action = args[1];
      if ([action isEqualToString:@"--quit"]) {
        NSArray* running = [NSRunningApplication
            runningApplicationsWithBundleIdentifier:
                [[NSBundle mainBundle] bundleIdentifier]];
        for (NSRunningApplication* app in running) {
          if (app.processIdentifier != getpid()) [app terminate];
        }
        return 0;
      }
      if ([action isEqualToString:@"--install"] ||
          [action isEqualToString:@"--register-input-source"]) {
        [installer registerInputSource];
        return 0;
      }
      if ([action isEqualToString:@"--enable-input-source"]) {
        [installer enableInputSource];
        return 0;
      }
      if ([action isEqualToString:@"--select-input-source"]) {
        [installer selectInputSource];
        return 0;
      }
      if ([action isEqualToString:@"--disable-input-source"]) {
        [installer disableInputSource];
        return 0;
      }
      if ([action isEqualToString:@"--help"]) {
        PrintHelp();
        return 0;
      }
      if ([action isEqualToString:@"--query"]) {
        // 调试/冒烟：不经 IMK 直接走核心查询，打印候选 JSON。
        if (args.count < 3) {
          fprintf(stderr, "usage: Shouzibilu --query <input>\n");
          return 1;
        }
        SZBLEngine* engine =
            [[SZBLEngine alloc] initWithSupportDir:[SZBAppDelegate supportDir]];
        NSArray* candidates = [engine candidatesForInput:args[2]];
        NSData* json = [NSJSONSerialization dataWithJSONObject:candidates
                                                       options:0
                                                         error:nil];
        puts([[NSString alloc] initWithData:json
                                   encoding:NSUTF8StringEncoding]
                 .UTF8String);
        return 0;
      }
      fprintf(stderr, "Unknown action: %s\n\n", action.UTF8String);
      PrintHelp();
      return 1;
    }

    // 输入法服务主循环。
    NSBundle* mainBundle = [NSBundle mainBundle];
    NSString* connectionName =
        [mainBundle objectForInfoDictionaryKey:@"InputMethodConnectionName"];
    IMKServer* server =
        [[IMKServer alloc] initWithName:connectionName
                       bundleIdentifier:[mainBundle bundleIdentifier]];
    (void)server;

    NSApplication* app = [NSApplication sharedApplication];
    SZBAppDelegate* delegate = [[SZBAppDelegate alloc] init];
    app.delegate = delegate;
    [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
    NSLog(@"Shouzibilu reporting!");
    [app run];
  }
  return 0;
}
