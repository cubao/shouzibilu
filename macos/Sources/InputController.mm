// ShouzibiluInputController - IMKInputController 子类
//
// 按键 -> 拼音缓冲 -> np_query -> 自绘候选窗（IMKCandidates 在 macOS 26
// 不渲染候选文字，弃用）-> 上屏 + np_commit。
//
// 交互（与 web 版 ime-editor.js 语义对齐）：
//   字母/'      追加输入
//   空格/Tab/1  上屏高亮行第 1 列；2 3 8 9 上屏第 2-5 列（好按的键位）
//   ↑↓ / , .    高亮行 ±1（整行蓝底，跨窗口自动滚动）
//   ← / →       组词光标按音节边界左/右移（查询只取光标前缀，
//               供逐字/逐词确认造词）；Home/End 到首/末边界
//   PgUp PgDn / - = [ ]   高亮行 ±4（翻页）
//   回车        原文上屏；Esc 清除；退格删码（并中止学习会话）
//   Shift 单击  切换中/英文；Shift+字母 先原文上屏再透出
//   标点        顶上屏高亮行首列后插对应中文标点
//   空缓冲 ,    上屏「，」（config.json 开 dynamic_comma 才进动态词模式）
//   连续分次上屏拼出的 2-8 字串自动学习为新词（np_learn_word）
//   Cmd/Ctrl/Opt 组合键一律透给应用
//
// Distributed under the BSD License.
#import <InputMethodKit/InputMethodKit.h>

#import "AppDelegate.h"
#import "CandidatePanel.h"
#import "Engine.h"

// 虚拟键码 kVK_* 来自 HIToolbox/Events.h（已由 InputMethodKit 头引入）。

static const NSInteger kCols = 5;         // 每行候选数（列选择键 ␣ 2 3 8 9）
static const NSInteger kVisibleRows = 4;  // 候选窗可见行数（每页 5×4=20）

// 中文标点映射（web 版 PUNCT 表的 macOS 简化版）。
static NSString* MapPunct(unichar ch) {
  switch (ch) {
    case ',': return @"，";
    case '.': return @"。";
    case '?': return @"？";
    case '!': return @"！";
    case ';': return @"；";
    case ':': return @"：";
    case '(': return @"（";
    case ')': return @"）";
    case '[': return @"【";
    case ']': return @"】";
    case '<': return @"《";
    case '>': return @"》";
    default: return nil;
  }
}

@interface ShouzibiluInputController : IMKInputController
@end

@implementation ShouzibiluInputController {
  SZBCandidatePanel* _panel;       // 懒创建的自绘候选窗
  NSMutableString* _buffer;        // 原始输入（小写字母与 '；动态词模式以 , 开头）
  NSArray<NSDictionary*>* _candidateData;
  NSInteger _selectedRow;          // 高亮行（全局行号，每行 kCols 个）
  NSInteger _rowStart;             // 候选窗首行（全局行号）
  NSInteger _caret;                // 组词光标（buffer 内下标，查询只取前缀）
  NSArray<NSNumber*>* _boundaries; // 音节边界（caret 左右移动的站位）
  BOOL _dynamicMode;               // buffer 以 , 开头：动态词模式
  BOOL _shiftAlone;                // shift 按下后未组合其它键
  // 学习会话（同 web 版 sessionAccum/finishSession）：连续分次上屏
  // 拼出的 2-8 字串，在组字清空时整体学习为新词。
  NSMutableArray<NSString*>* _sessionKeys;
  NSMutableString* _sessionText;
  NSInteger _sessionCommits;
}

- (instancetype)initWithServer:(IMKServer*)server
                      delegate:(id)delegate
                        client:(id)inputClient {
  self = [super initWithServer:server delegate:delegate client:inputClient];
  if (self) {
    _buffer = [NSMutableString string];
    _candidateData = @[];
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

// 注意：IMK 的事件分发选择器是 handleEvent:client:（IMKServerInput
// 协议方法，Apple 文档中的 handle(_:client:) 是它的 Swift 名）。
// 若只实现 handle:client:，控制器等于没实现任何输入入口，IMK 会退回
// keybinding 通道（NSKeyBindingManager interpretKeyEvents:）：字母经
// 默认 insertText: 原样进应用（表现为"只能打字母"），FlagsChanged
// 事件则被断言拒收抛异常 —— 2026-07 在 macOS 26 实测排障结论，
// 与 Squirrel 二进制的实现方式核对过。
- (BOOL)handleEvent:(NSEvent*)event client:(id)sender {
  @try {
    return [self handleEventImpl:event client:sender];
  } @catch (NSException* e) {
    NSLog(@"Shouzibilu: EXCEPTION in handleEvent: %@ — %@\n%@", e.name,
          e.reason, [e.callStackSymbols componentsJoinedByString:@"\n"]);
    return NO;
  }
}

- (BOOL)handleEventImpl:(NSEvent*)event client:(id)sender {
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

  // 小写字母：在光标处插入（动态词模式下同样追加）
  if (ch >= 'a' && ch <= 'z') {
    [_buffer insertString:[NSString stringWithFormat:@"%c", ch]
                  atIndex:_caret];
    _caret++;
    [self updateComposition:client];
    return YES;
  }
  // 大写字母（shift/caps）：原文上屏已有输入，字母透给应用
  if (ch >= 'A' && ch <= 'Z') {
    if (_buffer.length) [self commitLiteral:client];
    return NO;
  }
  // 音节分隔符（动态词模式不用）
  if (ch == '\'' && !_dynamicMode && _caret > 0 &&
      [_buffer characterAtIndex:_caret - 1] != '\'') {
    [_buffer insertString:@"'" atIndex:_caret];
    _caret++;
    [self updateComposition:client];
    return YES;
  }
  // 空缓冲：逗号（开了 dynamic_comma 才进动态词模式），可映射标点直接上屏
  if (_buffer.length == 0) {
    if (ch == ',' && !shift && [[self engine] dynamicCommaEnabled]) {
      [_buffer appendString:@","];
      _caret = 1;
      [self updateComposition:client];
      return YES;
    }
    NSString* punct = MapPunct(ch);
    if (punct && !shift) {
      [client insertText:punct replacementRange:NSMakeRange(NSNotFound, 0)];
      return YES;
    }
    return NO;
  }

  switch (keyCode) {
    case kVK_Delete:
      if (_caret > 0) {
        [self abortSession];  // 与 web 版一致：退格中止学习会话
        [_buffer deleteCharactersInRange:NSMakeRange(_caret - 1, 1)];
        _caret--;
      }
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
      if (_dynamicMode && _candidateData.count == 0) {
        [self commitDynamicFallback:client];
      } else if (_candidateData.count == 0) {
        [self commitLiteral:client];
      } else {
        [self commitColumn:0 client:client];
      }
      return YES;
    case kVK_UpArrow:
      [self moveRowBy:-1];
      return YES;
    case kVK_DownArrow:
      [self moveRowBy:1];
      return YES;
    case kVK_LeftArrow:
      [self moveCaretBy:-1 client:client];
      return YES;
    case kVK_RightArrow:
      [self moveCaretBy:1 client:client];
      return YES;
    case kVK_PageUp:
      [self moveRowBy:-kVisibleRows];
      return YES;
    case kVK_PageDown:
      [self moveRowBy:kVisibleRows];
      return YES;
    case kVK_Home:
    case kVK_End:
      [self moveCaretToEdge:(keyCode == kVK_End) client:client];
      return YES;
  }

  // 列选择：1 = 第 1 列，2 3 8 9 = 第 2-5 列（键位好按）
  if (!shift && (ch == '1' || ch == '2' || ch == '3' || ch == '8' || ch == '9')) {
    if (_candidateData.count == 0) {
      [self commitLiteral:client];
      return NO;  // 无候选：原文上屏，数字透给应用
    }
    [self commitColumn:(ch == '1') ? 0 : (ch == '2') ? 1 : (ch == '3') ? 2
                                       : (ch == '8') ? 3 : 4
                client:client];
    return YES;
  }
  // 其余数字键（4 5 6 7 0）：组字时吞掉
  if (!shift && ch >= '0' && ch <= '9') return YES;
  // , . 高亮行 ∓1（有候选时）；无候选按标点处理
  if ((ch == ',' || ch == '.') && !shift && _candidateData.count > 0) {
    [self moveRowBy:(ch == ',') ? -1 : 1];
    return YES;
  }
  // - = [ ] 翻页（有候选时）
  if ((ch == '-' || ch == '=' || ch == '[' || ch == ']') && !shift &&
      _candidateData.count > 0) {
    [self moveRowBy:((ch == '-' || ch == '[') ? -kVisibleRows : kVisibleRows)];
    return YES;
  }
  // 可映射标点：顶上屏高亮行首列/原文，再插中文标点
  NSString* punct = MapPunct(ch);
  if (punct && !shift) {
    if (_dynamicMode) {
      [self commitDynamicFallback:client];
    } else if (_candidateData.count) {
      [self commitCandidateAtIndex:_selectedRow * kCols client:client];
    } else {
      [self commitLiteral:client];
    }
    [client insertText:punct replacementRange:NSMakeRange(NSNotFound, 0)];
    return YES;
  }
  // 其余字符：顶上屏首选/原文，字符透给应用
  if (_candidateData.count && !_dynamicMode) {
    [self commitCandidateAtIndex:_selectedRow * kCols client:client];
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

#pragma mark - 组字与候选

- (void)updateComposition:(id<IMKTextInput, NSObject>)client {
  [self showMarkedText:client];

  if ([_buffer hasPrefix:@","]) {
    _dynamicMode = YES;
    _candidateData = [self dynamicCandidates];
  } else {
    _dynamicMode = NO;
    _boundaries = [[self engine] segmentBoundariesForInput:_buffer];
    NSString* prefix = [_buffer substringToIndex:_caret];
    _candidateData =
        prefix.length ? [[self engine] candidatesForInput:prefix] : @[];
  }
  _selectedRow = 0;
  _rowStart = 0;
  [self updateCandidatesDisplay:client];
}

// 光标移动后：只重查前缀（音节边界不动），重置高亮行。
- (void)caretChanged:(id<IMKTextInput, NSObject>)client {
  [self showMarkedText:client];
  NSString* prefix = [_buffer substringToIndex:_caret];
  _candidateData =
      prefix.length ? [[self engine] candidatesForInput:prefix] : @[];
  _selectedRow = 0;
  _rowStart = 0;
  [self updateCandidatesDisplay:client];
}

// 组词光标按音节边界左/右移（web 版 ArrowLeft/ArrowRight 同款）。
- (void)moveCaretBy:(NSInteger)delta
             client:(id<IMKTextInput, NSObject>)client {
  if (_dynamicMode || _boundaries.count == 0) return;
  NSInteger target = _caret;
  if (delta < 0) {
    for (NSInteger i = (NSInteger)_boundaries.count - 1; i >= 0; i--) {
      NSInteger b = _boundaries[i].integerValue;
      if (b < _caret) {
        target = b;
        break;
      }
    }
  } else {
    for (NSNumber* n in _boundaries) {
      if (n.integerValue > _caret) {
        target = n.integerValue;
        break;
      }
    }
  }
  if (target == _caret) return;
  _caret = target;
  [self caretChanged:client];
}

// Home/End：光标到首/末音节边界。
- (void)moveCaretToEdge:(BOOL)end
                 client:(id<IMKTextInput, NSObject>)client {
  if (_dynamicMode || _boundaries.count == 0) return;
  NSInteger target = end ? _boundaries.lastObject.integerValue
                         : _boundaries.firstObject.integerValue;
  if (target == _caret) return;
  _caret = target;
  [self caretChanged:client];
}

// 动态词候选：精确匹配优先，其余按 key 字典序（同 web 版）。
- (NSArray<NSDictionary*>*)dynamicCandidates {
  if (_buffer.length < 2) return @[];  // 裸逗号不进菜单
  NSDictionary<NSString*, NSString*>* mappings = [[self engine] mappings];
  NSMutableArray<NSDictionary*>* exact = [NSMutableArray array];
  NSMutableArray<NSDictionary*>* prefix = [NSMutableArray array];
  for (NSString* key in mappings) {
    if ([key isEqualToString:_buffer]) {
      [exact addObject:@{@"text" : mappings[key]}];
    } else if ([key hasPrefix:_buffer]) {
      [prefix addObject:@{@"text" : mappings[key], @"key" : key}];
    }
  }
  [prefix sortUsingComparator:^NSComparisonResult(NSDictionary* a,
                                                  NSDictionary* b) {
    return [a[@"key"] compare:b[@"key"]];
  }];
  NSMutableArray<NSDictionary*>* all =
      [NSMutableArray arrayWithArray:exact];
  [all addObjectsFromArray:prefix];
  if (all.count > 50) [all removeObjectsInRange:NSMakeRange(50, all.count - 50)];
  return all;
}

// 把可见窗口（_rowStart 起最多 kVisibleRows 行）喂给自绘候选窗。
- (void)updateCandidatesDisplay:(id<IMKTextInput, NSObject>)client {
  NSInteger total = (NSInteger)_candidateData.count;
  if (total == 0) {
    [_panel hide];
    return;
  }
  NSInteger totalRows = (total + kCols - 1) / kCols;
  _selectedRow = MIN(MAX(_selectedRow, 0), totalRows - 1);
  // 滚动窗口保持高亮行可见
  if (_selectedRow < _rowStart) _rowStart = _selectedRow;
  if (_selectedRow >= _rowStart + kVisibleRows)
    _rowStart = _selectedRow - kVisibleRows + 1;
  _rowStart = MIN(MAX(_rowStart, 0), MAX(totalRows - kVisibleRows, 0));

  NSInteger rowsShown = MIN(kVisibleRows, totalRows - _rowStart);
  NSMutableArray<NSArray<NSString*>*>* rows =
      [NSMutableArray arrayWithCapacity:rowsShown];
  for (NSInteger r = 0; r < rowsShown; r++) {
    NSInteger base = (_rowStart + r) * kCols;
    NSInteger n = MIN(kCols, total - base);
    NSMutableArray<NSString*>* row = [NSMutableArray arrayWithCapacity:n];
    for (NSInteger c = 0; c < n; c++) {
      [row addObject:_candidateData[base + c][@"text"]];
    }
    [rows addObject:row];
  }

  if (!_panel) _panel = [[SZBCandidatePanel alloc] init];
  [_panel updateWithRows:rows highlightRow:_selectedRow - _rowStart];
  [_panel showAtCaretRect:[self caretRect:client]];
}

- (NSRect)caretRect:(id<IMKTextInput, NSObject>)client {
  NSRect rect = NSZeroRect;
  // 用组字文本内下标的行矩形（Squirrel 同款）；
  // firstRectForCharacterRange: 要的是文档范围，组字时拿到的是
  // 垃圾值，表现为"候选窗离光标很远"。
  if ([client respondsToSelector:@selector(attributesForCharacterIndex:
                                           lineHeightRectangle:)]) {
    [client attributesForCharacterIndex:0 lineHeightRectangle:&rect];
  }
  if (NSIsEmptyRect(rect)) {  // 客户端不支持时退到鼠标位置
    NSPoint mouse = [NSEvent mouseLocation];
    rect = NSMakeRect(mouse.x, mouse.y, 1, 20);
  }
  return rect;
}

- (void)showMarkedText:(id<IMKTextInput, NSObject>)client {
  NSDictionary* attrs =
      @{NSUnderlineStyleAttributeName : @(NSUnderlineStyleSingle)};
  NSAttributedString* marked =
      [[NSAttributedString alloc] initWithString:_buffer attributes:attrs];
  [client setMarkedText:marked
        selectionRange:NSMakeRange(_caret, 0)
      replacementRange:NSMakeRange(NSNotFound, 0)];
}

- (void)resetState {
  [_buffer setString:@""];
  _candidateData = @[];
  _selectedRow = 0;
  _rowStart = 0;
  _caret = 0;
  _boundaries = @[];
  _dynamicMode = NO;
  [_panel hide];
}

- (void)clearComposition:(id<IMKTextInput, NSObject>)client {
  [self abortSession];
  [self resetState];
  [client setMarkedText:@""
        selectionRange:NSMakeRange(0, 0)
      replacementRange:NSMakeRange(NSNotFound, 0)];
}

// 原始输入原文上屏（回车/Esc 之外的"放弃组字"路径）。
- (void)commitLiteral:(id<IMKTextInput, NSObject>)client {
  if (_buffer.length == 0) return;
  NSString* text = [_buffer copy];
  [self abortSession];
  [self resetState];
  [client insertText:text replacementRange:NSMakeRange(NSNotFound, 0)];
}

// 动态词模式空格回退（同 web 版 commitDynamicBest）：
// 有候选上屏首选；无候选输出「，」+ 已输字母。
- (void)commitDynamicFallback:(id<IMKTextInput, NSObject>)client {
  if (_candidateData.count) {
    [self commitCandidateAtIndex:0 client:client];
    return;
  }
  NSString* rest = [_buffer substringFromIndex:1];
  [self abortSession];
  [self resetState];
  [client insertText:[NSString stringWithFormat:@"，%@", rest]
    replacementRange:NSMakeRange(NSNotFound, 0)];
}

// 上屏高亮行第 col 列（列越界则吞掉）。
- (void)commitColumn:(NSInteger)col
              client:(id<IMKTextInput, NSObject>)client {
  NSInteger index = _selectedRow * kCols + col;
  if (index >= (NSInteger)_candidateData.count) return;
  [self commitCandidateAtIndex:index client:client];
}

- (void)commitCandidateAtIndex:(NSInteger)index
                        client:(id<IMKTextInput, NSObject>)client {
  if (index < 0 || index >= (NSInteger)_candidateData.count) {
    [self commitLiteral:client];
    return;
  }
  NSDictionary* cand = _candidateData[index];
  NSString* text = cand[@"text"];

  if (_dynamicMode) {
    [self abortSession];  // 动态词不参与学习
    [self resetState];
    [client insertText:text replacementRange:NSMakeRange(NSNotFound, 0)];
    return;
  }

  NSUInteger consumed = [cand[@"consumed"] unsignedIntegerValue];
  if (consumed > _buffer.length) consumed = _buffer.length;
  // 候选可能只消费输入前缀，余码继续组字（与 web 版行为一致）。
  NSString* rest = [_buffer substringFromIndex:consumed];
  while ([rest hasPrefix:@"'"]) rest = [rest substringFromIndex:1];

  [self sessionAccumText:text segments:cand[@"segments"]];
  [[self engine] commitSegments:cand[@"segments"]];
  [_buffer setString:rest];
  _caret = _buffer.length;  // 上屏后光标回末尾（同 web 版）
  [client insertText:text replacementRange:NSMakeRange(NSNotFound, 0)];

  if (_buffer.length) {
    [self updateComposition:client];
  } else {
    [self finishSessionIfComplete];
    [self resetState];
  }
}

#pragma mark - 学习会话（同 web 版 ime-editor.js）

- (void)sessionAccumText:(NSString*)text
                segments:(NSArray<NSDictionary*>*)segments {
  if (!_sessionKeys) {
    _sessionKeys = [NSMutableArray array];
    _sessionText = [NSMutableString string];
    _sessionCommits = 0;
  }
  for (NSDictionary* seg in segments) {
    NSString* key = seg[@"key"];
    if (key) [_sessionKeys addObject:key];
  }
  [_sessionText appendString:text];
  _sessionCommits++;
}

- (void)finishSessionIfComplete {
  if (_sessionCommits >= 2 && _sessionText.length >= 2 &&
      _sessionText.length <= 8) {
    [[self engine] learnWord:_sessionText
                         key:[_sessionKeys componentsJoinedByString:@" "]];
  }
  [self abortSession];
}

- (void)abortSession {
  _sessionKeys = nil;
  _sessionText = nil;
  _sessionCommits = 0;
}

#pragma mark - 候选窗导航

// 高亮行移动 delta 行（±1 逐行，±kVisibleRows 翻页）。
- (void)moveRowBy:(NSInteger)delta {
  if (_candidateData.count == 0) return;
  NSInteger totalRows = ((NSInteger)_candidateData.count + kCols - 1) / kCols;
  _selectedRow = MIN(MAX(_selectedRow + delta, 0), totalRows - 1);
  [self updateCandidatesDisplay:[self client]];
}

#pragma mark - 会话生命周期

- (void)commitComposition:(id)sender {
  [self commitLiteral:sender];
}

- (void)deactivateServer:(id)sender {
  [self commitLiteral:sender];
  [super deactivateServer:sender];
}

#pragma mark - 输入法菜单

- (NSMenu*)menu {
  SZBAppDelegate* delegate = (SZBAppDelegate*)NSApp.delegate;
  NSMenu* menu = [[NSMenu alloc] initWithTitle:@"手自笔录"];

  NSMenuItem* toggle = [[NSMenuItem alloc]
      initWithTitle:delegate.asciiMode ? @"切换为中文" : @"切换为英文"
             action:@selector(toggleASCIIModeAction:)
      keyEquivalent:@""];
  toggle.target = self;
  [menu addItem:toggle];
  [menu addItem:[NSMenuItem separatorItem]];

  NSMenuItem* reload =
      [[NSMenuItem alloc] initWithTitle:@"重新加载词库与配置"
                                 action:@selector(reloadEngineAction:)
                          keyEquivalent:@""];
  reload.target = self;
  [menu addItem:reload];

  NSMenuItem* openDir =
      [[NSMenuItem alloc] initWithTitle:@"打开配置目录（config/mappings/词表）"
                                 action:@selector(openSupportDirAction:)
                          keyEquivalent:@""];
  openDir.target = self;
  [menu addItem:openDir];
  return menu;
}

// 菜单 action 挂在控制器自身（挂 AppDelegate 时输入法菜单里不触发）。
- (void)toggleASCIIModeAction:(id)sender {
  SZBAppDelegate* delegate = (SZBAppDelegate*)NSApp.delegate;
  delegate.asciiMode = !delegate.asciiMode;
}

- (void)reloadEngineAction:(id)sender {
  [[self engine] reload];
}

- (void)openSupportDirAction:(id)sender {
  // openURL: 在输入法菜单路径下常无效，selectFile: 走 Finder 可靠。
  [[NSWorkspace sharedWorkspace]
       selectFile:nil
      inFileViewerRootedAtPath:[[self engine].supportDir path]];
}

@end
