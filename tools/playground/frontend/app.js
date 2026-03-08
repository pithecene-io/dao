import {
  EditorView,
  basicSetup,
} from "https://esm.sh/@codemirror/basic-setup@0.20.0";
import { EditorState } from "https://esm.sh/@codemirror/state@6.5.2";
import {
  Decoration,
  ViewPlugin,
  WidgetType,
} from "https://esm.sh/@codemirror/view@6.36.5";
import {
  oneDark,
} from "https://esm.sh/@codemirror/theme-one-dark@6.1.2";

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

const categoryToClass = {
  "keyword": "dao-keyword",
  "literal.number": "dao-literal-number",
  "literal.string": "dao-literal-string",
  "literal.bool": "dao-literal-bool",
  "operator": "dao-operator",
  "punctuation": "dao-punctuation",
  "identifier": "dao-identifier",
  "error": "dao-error",
};

function buildDecorations(view) {
  const decorations = [];
  const docLength = view.state.doc.length;

  for (const tok of currentTokens) {
    const from = tok.offset;
    const to = tok.offset + tok.length;
    if (from >= docLength || to > docLength || from >= to) continue;

    const cls = categoryToClass[tok.category];
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

const defaultSource = `fn main(): int32
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

    // Update tokens and refresh decorations.
    currentTokens = data.tokens || [];
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
