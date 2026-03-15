import type { EditorView } from "@codemirror/view";
import type { AnalyzeResponse } from "./types";
import { setTokens } from "./highlighting";
import { renderDiagnostics } from "./diagnostics";
import { getSource } from "./editor";

let analyzeTimer: ReturnType<typeof setTimeout> | null = null;
let analyzeSeq = 0;
let editorView: EditorView | null = null;

const DEBOUNCE_MS = 300;

/** Bind the analysis module to the editor view. */
export function initAnalysis(view: EditorView): void {
  editorView = view;
}

/** Schedule a debounced analysis request. */
export function scheduleAnalyze(): void {
  if (analyzeTimer) clearTimeout(analyzeTimer);
  analyzeTimer = setTimeout(doAnalyze, DEBOUNCE_MS);
}

/** Run analysis immediately. */
export async function doAnalyze(): Promise<void> {
  if (!editorView) return;

  const source = getSource();
  const seq = ++analyzeSeq;

  try {
    const resp = await fetch("/api/analyze", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ source }),
    });

    if (seq !== analyzeSeq) return;
    if (!resp.ok) return;

    const data: AnalyzeResponse = await resp.json();
    if (seq !== analyzeSeq) return;

    // Update semantic tokens and force decoration rebuild.
    setTokens(data.semanticTokens || [], editorView);

    // Update IR panels.
    setText("ast-output", data.ast);
    setText("hir-output", data.hir);
    setText("mir-output", data.mir);
    setText("llvm-ir-output", data.llvm_ir);

    // Update diagnostics.
    renderDiagnostics(data.diagnostics || []);
  } catch (err) {
    console.error("analyze failed:", err);
  }
}

function setText(id: string, value: string): void {
  const el = document.getElementById(id);
  if (el) el.textContent = value || "";
}
