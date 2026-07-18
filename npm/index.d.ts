// @cubao/naive-pinyin 类型声明
declare module "@cubao/naive-pinyin" {
  export interface Segment {
    key: string;   // 空格分隔的拼音音节
    word: string;  // 该段文本
  }

  export interface Candidate {
    text: string;
    consumed: number;  // 消耗的输入字符数
    score: number;
    segments: Segment[];
  }

  export interface QueryResult {
    input: string;
    candidates: Candidate[];
    error?: string;
  }

  export interface SegmentResult {
    input: string;
    boundaries: number[];  // 组词光标站位（音节 DAG 全边界）
    path: number[];        // 贪心最长边主切分（preedit 显示用）
    error?: string;
  }

  export interface UserFreqEntry {
    pinyin: string;
    word: string;
    count: number;
  }

  export interface EngineConfig {
    shuangpin?: { map: Record<string, string> };
    fuzzy?: [string, string][];
    max_candidates?: number;
    segment_penalty?: number;
    user_boost?: { first?: number; inc?: number; max?: number };
    user_freq?: UserFreqEntry[];
    user_words?: { pinyin: string; word: string; freq?: number }[];
  }

  export class Engine {
    static create(
      config?: EngineConfig,
      dictData?: Uint8Array
    ): Promise<Engine>;
    query(input: string): QueryResult;
    segment(input: string): SegmentResult;
    commit(segments: Segment[]): void;
    learnWord(key: string, word: string): void;
    dumpUser(): UserFreqEntry[];
    destroy(): void;
  }

  export const ziranma: { shuangpin: { map: Record<string, string> } };
  export function createEngine(
    config?: EngineConfig,
    dictData?: Uint8Array
  ): Promise<Engine>;

  // 浏览器 IME 编辑器（ime-editor.js）：textarea 全键盘接管 + 候选弹窗
  // + 动态词 + 精简 vim。仅在浏览器环境可用。
  export interface ImeEditorApi {
    attach(textarea: HTMLTextAreaElement, opts: Record<string, unknown>): {
      setOption(o: Record<string, unknown>): void;
      getMode(): { mode: string; english: boolean };
      focus(): void;
      destroy(): void;
    };
    LAYOUTS: Record<string, Record<string, [string, string]>>;
    DEFAULT_MAPPINGS: Record<string, string>;
  }
  export const imeEditor: ImeEditorApi;
}
