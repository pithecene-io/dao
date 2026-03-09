// All esm.sh imports pin @codemirror/state and @codemirror/view via ?deps
// so that only one instance of each package is loaded at runtime.
// Without this, instanceof checks inside CodeMirror break.
import {
  basicSetup,
} from "https://esm.sh/codemirror@6.0.2?deps=@codemirror/state@6.5.4,@codemirror/view@6.39.16";
import { EditorState } from "https://esm.sh/@codemirror/state@6.5.4";
import {
  Decoration,
  EditorView,
  ViewPlugin,
} from "https://esm.sh/@codemirror/view@6.39.16";
import {
  oneDark,
} from "https://esm.sh/@codemirror/theme-one-dark@6.1.3?deps=@codemirror/state@6.5.4,@codemirror/view@6.39.16";

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

let currentTokens = [];
let analyzeTimer = null;
let analyzeSeq = 0;
const DEBOUNCE_MS = 300;

// ---------------------------------------------------------------------------
// Token decoration plugin
// ---------------------------------------------------------------------------

// Map semantic token kinds from CONTRACT_LANGUAGE_TOOLING.md to CSS classes.
// Prefix groups share a class so highlighting degrades gracefully.
function semanticKindToClass(kind) {
  if (kind.startsWith("keyword.")) return "dao-keyword";
  if (kind.startsWith("decl.")) return "dao-decl";
  if (kind.startsWith("type.")) return "dao-type";
  if (kind.startsWith("use.variable.")) return "dao-variable";
  if (kind === "use.function") return "dao-decl";
  if (kind === "use.field") return "dao-field";
  if (kind === "use.module" || kind === "decl.module") return "dao-module";
  if (kind.startsWith("mode.")) return "dao-mode";
  if (kind.startsWith("resource.")) return "dao-resource";
  if (kind === "lambda.param") return "dao-lambda-param";
  if (kind === "literal.number") return "dao-literal-number";
  if (kind === "literal.string") return "dao-literal-string";
  if (kind.startsWith("operator.")) return "dao-operator";
  if (kind === "punctuation") return "dao-punctuation";
  return null;
}

function buildDecorations(view) {
  const decorations = [];
  const docLength = view.state.doc.length;

  for (const tok of currentTokens) {
    const from = tok.offset;
    const to = tok.offset + tok.length;
    if (from >= docLength || to > docLength || from >= to) continue;

    const cls = semanticKindToClass(tok.kind);
    if (!cls) continue;

    decorations.push(
      Decoration.mark({ class: cls }).range(from, to)
    );
  }

  return Decoration.set(decorations, true);
}

const tokenHighlighter = ViewPlugin.fromClass(
  class {
    constructor(view) {
      this.decorations = buildDecorations(view);
    }
    update(update) {
      // Rebuild when tokens change (triggered by dispatching a no-op)
      this.decorations = buildDecorations(update.view);
    }
  },
  {
    decorations: (v) => v.decorations,
  }
);

// ---------------------------------------------------------------------------
// Editor setup
// ---------------------------------------------------------------------------

const defaultSource = `fn main(): i32
    print("hello, dao")
    0
`;

const editor = new EditorView({
  state: EditorState.create({
    doc: defaultSource,
    extensions: [
      basicSetup,
      oneDark,
      tokenHighlighter,
      EditorView.updateListener.of((update) => {
        if (update.docChanged) {
          scheduleAnalyze();
        }
      }),
    ],
  }),
  parent: document.getElementById("editor-container"),
});

// ---------------------------------------------------------------------------
// Analysis
// ---------------------------------------------------------------------------

function scheduleAnalyze() {
  if (analyzeTimer) clearTimeout(analyzeTimer);
  analyzeTimer = setTimeout(doAnalyze, DEBOUNCE_MS);
}

async function doAnalyze() {
  const source = editor.state.doc.toString();
  const seq = ++analyzeSeq;

  try {
    const resp = await fetch("/api/analyze", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ source }),
    });

    // Discard stale responses — a newer request has been issued.
    if (seq !== analyzeSeq) return;

    if (!resp.ok) return;
    const data = await resp.json();

    // Double-check after await: another request may have started
    // while we were parsing the response body.
    if (seq !== analyzeSeq) return;

    // Update tokens with semantic classification and refresh decorations.
    currentTokens = data.semanticTokens || [];
    // Force decoration rebuild by dispatching empty transaction.
    editor.dispatch({});

    // Update AST panel.
    document.getElementById("ast-output").textContent = data.ast || "";

    // Update diagnostics panel.
    renderDiagnostics(data.diagnostics || []);
  } catch (err) {
    console.error("analyze failed:", err);
  }
}

function renderDiagnostics(diagnostics) {
  const container = document.getElementById("diagnostics-output");

  if (diagnostics.length === 0) {
    container.innerHTML = '<div class="no-diagnostics">No errors</div>';
    return;
  }

  container.innerHTML = diagnostics
    .map(
      (d) =>
        `<div class="diagnostic"><span class="location">${d.line}:${d.col}</span>${escapeHtml(d.message)}</div>`
    )
    .join("");
}

function escapeHtml(text) {
  const div = document.createElement("div");
  div.textContent = text;
  return div.innerHTML;
}

// ---------------------------------------------------------------------------
// Example loader
// ---------------------------------------------------------------------------

async function loadExamples() {
  try {
    const resp = await fetch("/api/examples");
    if (!resp.ok) return;
    const data = await resp.json();

    const select = document.getElementById("example-select");
    for (const example of data.examples || []) {
      const option = document.createElement("option");
      option.value = example.name;
      option.textContent = example.name;
      select.appendChild(option);
    }

    select.addEventListener("change", async () => {
      const name = select.value;
      if (!name) return;

      try {
        const resp = await fetch(`/api/examples/${encodeURIComponent(name)}`);
        if (!resp.ok) return;
        const data = await resp.json();

        editor.dispatch({
          changes: {
            from: 0,
            to: editor.state.doc.length,
            insert: data.source,
          },
        });
      } catch (err) {
        console.error("failed to load example:", err);
      }
    });
  } catch (err) {
    console.error("failed to load example list:", err);
  }
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

loadExamples();
doAnalyze();
