// ShouzibiluInputController - IMKInputController 子类
//
// 按键 -> 拼音缓冲 -> np_query -> IMKCandidates 候选窗 -> 上屏 + np_commit。
// 交互参考 macOS 自带拼音与 Squirrel：
//   字母/'      追加输入
//   空格/Tab    上屏高亮候选
//   数字 1-9    上屏候选窗可见行
//   ↑↓          移动高亮；←→ / PgUp PgDn / - = [ ] 翻页
//   回车        原文上屏；Esc 清除；退格删码
//   Shift 单击  切换中/英文；Shift+字母 先原文上屏再透出
//   标点        顶上屏首选后透给应用
//   Cmd/Ctrl/Opt 组合键一律透给应用
//
// Distributed under the BSD License.
#import <InputMethodKit/InputMethodKit.h>

#import "AppDelegate.h"
#import "Engine.h"

// 虚拟键码 kVK_* 来自 HIToolbox/Events.h（已由 InputMethodKit 头引入）。

static const NSInteger kPageSize = 9;  // 与默认选词键 1-9 对应的一页

@interface ShouzibiluInputController : IMKInputController
@end

@implementation ShouzibiluInputController {
  IMKCandidates* _candidates;      // 懒创建的候选窗
  NSMutableString* _buffer;        // 原始输入（小写字母与 '）
  NSArray<NSDictionary*>* _candidateData;
  NSArray<NSNumber*>* _candidateIdents;  // 与 _candidateData 对齐的 IMK 标识
  NSInteger _selected;                   // 高亮候选下标
  BOOL _shiftAlone;                      // shift 按下后未组合其它键
}

- (instancetype)initWithServer:(IMKServer*)server
                      delegate:(id)delegate
                        client:(id)inputClient {
  self = [super initWithServer:server delegate:delegate client:inputClient];
  if (self) {
    _buffer = [NSMutableString string];
    _candidateData = @[];
    _candidateIdents = @[];
  }
  return self;
}

- (SZBLEngine*)engine {
  return ((SZBAppDelegate*)NSApp.delegate).engine;
}

- (BOOL)asciiMode {
  return ((SZBAppDelegate*)NSApp.delegate).asciiMode;
}

- (NSUInteger)recognizedEvents:(id)sender {
  return NSEventMaskKeyDown | NSEventMaskFlagsChanged;
}

#pragma mark - 事件入口

- (BOOL)handle:(NSEvent*)event client:(id)sender {
  id<IMKTextInput, NSObject> client = sender;

  if (event.type == NSEventTypeFlagsChanged) {
    [self handleFlagsChanged:event client:client];
    return NO;  // 修饰键事件本身不吞
  }
  if (event.type != NSEventTypeKeyDown) return NO;

  _shiftAlone = NO;  // shift 与其它键组合，不算单击

  NSEventModifierFlags mods =
      event.modifierFlags &
      (NSEventModifierFlagCommand | NSEventModifierFlagControl |
       NSEventModifierFlagOption);
  if (mods != 0) {
    if (_buffer.length) [self commitLiteral:client];
    return NO;
  }
  if ([self asciiMode]) return NO;

  unsigned short keyCode = event.keyCode;
  BOOL shift = (event.modifierFlags & NSEventModifierFlagShift) != 0;
  NSString* chars = event.charactersIgnoringModifiers;
  unichar ch = chars.length ? [chars characterAtIndex:0] : 0;

  // 小写字母：组码
  if (ch >= 'a' && ch <= 'z') {
    [_buffer appendFormat:@"%c", ch];
    [self updateComposition:client];
    return YES;
  }
  // 大写字母（shift/caps）：原文上屏已有输入，字母透给应用
  if (ch >= 'A' && ch <= 'Z') {
    if (_buffer.length) [self commitLiteral:client];
    return NO;
  }
  // 音节分隔符
  if (ch == '\'' && _buffer.length > 0 && ![_buffer hasSuffix:@"'"]) {
    [_buffer appendString:@"'"];
    [self updateComposition:client];
    return YES;
  }
  // 以下仅在组字时接管
  if (_buffer.length == 0) return NO;

  switch (keyCode) {
    case kVK_Delete:
      [_buffer deleteCharactersInRange:NSMakeRange(_buffer.length - 1, 1)];
      if (_buffer.length) {
        [self updateComposition:client];
      } else {
        [self clearComposition:client];
      }
      return YES;
    case kVK_Escape:
      [self clearComposition:client];
      return YES;
    case kVK_Return:
    case kVK_ANSI_KeypadEnter:
      [self commitLiteral:client];
      return YES;
    case kVK_Space:
    case kVK_Tab:
      [self commitCandidateAtIndex:_selected client:client];
      return YES;
    case kVK_UpArrow:
      [self moveSelectionBy:-1];
      return YES;
    case kVK_DownArrow:
      [self moveSelectionBy:1];
      return YES;
    case kVK_LeftArrow:
    case kVK_PageUp:
      [self moveSelectionBy:-kPageSize];
      return YES;
    case kVK_RightArrow:
    case kVK_PageDown:
      [self moveSelectionBy:kPageSize];
      return YES;
    case kVK_Home:
    case kVK_End:
      return YES;  // 吞掉，防止光标在组字时乱跑
  }

  // 数字选词：候选窗可见行
  if (ch >= '1' && ch <= '9' && !shift) {
    if (_candidateData.count == 0) {
      [self commitLiteral:client];
      return NO;
    }
    [self commitVisibleLine:(ch - '1') client:client];
    return YES;
  }
  // - = [ ] 翻页（有候选时）
  if ((ch == '-' || ch == '=' || ch == '[' || ch == ']') && !shift &&
      _candidateData.count > 0) {
    [self moveSelectionBy:(ch == '-' || ch == '[') ? -kPageSize : kPageSize];
    return YES;
  }
  // 其余字符（标点等）：顶上屏首选/原文，字符透给应用
  if (_candidateData.count) {
    [self commitCandidateAtIndex:_selected client:client];
  } else {
    [self commitLiteral:client];
  }
  return NO;
}

- (void)handleFlagsChanged:(NSEvent*)event
                    client:(id<IMKTextInput, NSObject>)client {
  unsigned short keyCode = event.keyCode;
  if (keyCode != 0x38 /* shift L */ && keyCode != 0x3C /* shift R */) return;
  if (event.modifierFlags & NSEventModifierFlagShift) {
    _shiftAlone = YES;  // 按下；是否"单击"看后续有无 keyDown
  } else if (_shiftAlone) {
    _shiftAlone = NO;
    if (_buffer.length) [self commitLiteral:client];
    SZBAppDelegate* delegate = (SZBAppDelegate*)NSApp.delegate;
    delegate.asciiMode = !delegate.asciiMode;
  }
}

#pragma mark - 组字与上屏

- (void)updateComposition:(id<IMKTextInput, NSObject>)client {
  [self showMarkedText:client];

  _candidateData = [[self engine] candidatesForInput:_buffer];
  _selected = 0;
  if (_candidateData.count == 0) {
    _candidateIdents = @[];
    [_candidates hide];
    return;
  }

  NSMutableArray<NSString*>* strings =
      [NSMutableArray arrayWithCapacity:_candidateData.count];
  for (NSDictionary* cand in _candidateData) {
    [strings addObject:cand[@"text"]];
  }
  if (!_candidates) {
    _candidates = [[IMKCandidates alloc]
        initWithServer:[self server]
             panelType:kIMKSingleColumnScrollingCandidatePanel];
  }
  [_candidates setCandidateData:strings];
  // 预先取出各候选的标识，之后按标识定位（重复文本共享标识，影响可忽略）。
  NSMutableArray<NSNumber*>* idents =
      [NSMutableArray arrayWithCapacity:strings.count];
  for (NSString* s in strings) {
    [idents addObject:@([_candidates candidateStringIdentifier:s])];
  }
  _candidateIdents = idents;
  [_candidates updateCandidates];
  [_candidates
      selectCandidateWithIdentifier:_candidateIdents[0].integerValue];
  [_candidates show:kIMKLocateCandidatesBelowHint];
}

- (void)showMarkedText:(id<IMKTextInput, NSObject>)client {
  NSDictionary* attrs =
      @{NSUnderlineStyleAttributeName : @(NSUnderlineStyleSingle)};
  NSAttributedString* marked =
      [[NSAttributedString alloc] initWithString:_buffer attributes:attrs];
  [client setMarkedText:marked
        selectionRange:NSMakeRange(_buffer.length, 0)
      replacementRange:NSMakeRange(NSNotFound, 0)];
}

- (void)resetState {
  [_buffer setString:@""];
  _candidateData = @[];
  _candidateIdents = @[];
  _selected = 0;
  [_candidates hide];
}

- (void)clearComposition:(id<IMKTextInput, NSObject>)client {
  [self resetState];
  [client setMarkedText:@""
        selectionRange:NSMakeRange(0, 0)
      replacementRange:NSMakeRange(NSNotFound, 0)];
}

// 原始输入原文上屏（回车/Esc 之外的"放弃组字"路径）。
- (void)commitLiteral:(id<IMKTextInput, NSObject>)client {
  if (_buffer.length == 0) return;
  NSString* text = [_buffer copy];
  [self resetState];
  [client insertText:text replacementRange:NSMakeRange(NSNotFound, 0)];
}

- (void)commitCandidateAtIndex:(NSInteger)index
                        client:(id<IMKTextInput, NSObject>)client {
  if (index < 0 || index >= (NSInteger)_candidateData.count) {
    [self commitLiteral:client];
    return;
  }
  NSDictionary* cand = _candidateData[index];
  NSString* text = cand[@"text"];
  NSUInteger consumed = [cand[@"consumed"] unsignedIntegerValue];
  if (consumed > _buffer.length) consumed = _buffer.length;
  // 候选可能只消费输入前缀，余码继续组字（与 web 版行为一致）。
  NSString* rest = [_buffer substringFromIndex:consumed];
  while ([rest hasPrefix:@"'"]) rest = [rest substringFromIndex:1];

  [[self engine] commitSegments:cand[@"segments"]];
  [_buffer setString:rest];
  [client insertText:text replacementRange:NSMakeRange(NSNotFound, 0)];

  if (_buffer.length) {
    [self updateComposition:client];
  } else {
    [self resetState];
  }
}

#pragma mark - 候选窗导航

- (void)moveSelectionBy:(NSInteger)delta {
  if (_candidateIdents.count == 0) return;
  NSInteger count = (NSInteger)_candidateIdents.count;
  _selected = MIN(MAX(_selected + delta, 0), count - 1);
  [_candidates
      selectCandidateWithIdentifier:_candidateIdents[_selected].integerValue];
}

- (void)commitVisibleLine:(NSInteger)line
                   client:(id<IMKTextInput, NSObject>)client {
  NSInteger ident = [_candidates candidateIdentifierAtLineNumber:line];
  if (ident == NSNotFound) return;
  NSUInteger index = [_candidateIdents indexOfObject:@(ident)];
  if (index == NSNotFound) return;
  _selected = (NSInteger)index;
  [_candidates selectCandidateWithIdentifier:ident];
  [self commitCandidateAtIndex:_selected client:client];
}

#pragma mark - 会话生命周期

- (void)commitComposition:(id)sender {
  [self commitLiteral:sender];
}

- (void)deactivateServer:(id)sender {
  [self commitLiteral:sender];
  [super deactivateServer:sender];
}

- (NSMenu*)menu {
  SZBAppDelegate* delegate = (SZBAppDelegate*)NSApp.delegate;
  NSMenu* menu = [[NSMenu alloc] initWithTitle:@"手自笔录"];

  NSMenuItem* toggle = [[NSMenuItem alloc]
      initWithTitle:delegate.asciiMode ? @"切换为中文" : @"切换为英文"
             action:@selector(toggleASCIIMode:)
      keyEquivalent:@""];
  toggle.target = delegate;
  [menu addItem:toggle];
  [menu addItem:[NSMenuItem separatorItem]];

  NSMenuItem* reload =
      [[NSMenuItem alloc] initWithTitle:@"重新加载词库与配置…"
                                 action:@selector(reloadEngine:)
                          keyEquivalent:@""];
  reload.target = delegate;
  [menu addItem:reload];

  NSMenuItem* openDir =
      [[NSMenuItem alloc] initWithTitle:@"打开用户目录…"
                                 action:@selector(openSupportDir:)
                          keyEquivalent:@""];
  openDir.target = delegate;
  [menu addItem:openDir];
  return menu;
}

@end
