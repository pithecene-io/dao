import type { EditorView } from "@codemirror/view";
import { getSource } from "./editor";

interface GotoDefResponse {
  offset: number;
  length: number;
  line: number;
  col: number;
}

export function initGotoDef(view: EditorView): void {
  view.dom.addEventListener("click", async (e: MouseEvent) => {
    // Ctrl+Click (or Cmd+Click on Mac) for go-to-definition.
    if (!e.ctrlKey && !e.metaKey) return;

    e.preventDefault();

    const pos = view.posAtCoords({ x: e.clientX, y: e.clientY });
    if (pos === null) return;

    const source = getSource();

    try {
      const resp = await fetch("/api/goto-def", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ source, offset: pos }),
      });

      if (!resp.ok) return;

      const data: GotoDefResponse | null = await resp.json();
      if (!data) return;

      // Move cursor to the definition and scroll it into view.
      view.dispatch({
        selection: { anchor: data.offset },
        scrollIntoView: true,
      });
    } catch {
      // Silently ignore errors.
    }
  });
}
