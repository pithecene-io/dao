/** Semantic token from /api/analyze. */
export interface SemanticToken {
  kind: string;
  offset: number;
  length: number;
  line: number;
  col: number;
}

/** Diagnostic from /api/analyze or /api/run. */
export interface DiagnosticEntry {
  severity: string;
  offset: number;
  length: number;
  line: number;
  col: number;
  message: string;
}

/** Response shape from /api/analyze. */
export interface AnalyzeResponse {
  tokens: unknown[];
  semanticTokens: SemanticToken[];
  ast: string;
  hir: string;
  mir: string;
  llvm_ir: string;
  diagnostics: DiagnosticEntry[];
}

/** Response shape from /api/run. */
export interface RunResponse {
  stdout: string;
  stderr: string;
  exit_code: number;
  diagnostics: DiagnosticEntry[];
}

/** Example entry from /api/examples. */
export interface ExampleEntry {
  name: string;
}

/** Response shape from /api/examples. */
export interface ExamplesListResponse {
  examples: ExampleEntry[];
}

/** Response shape from /api/examples/:name. */
export interface ExampleSourceResponse {
  name: string;
  source: string;
}
