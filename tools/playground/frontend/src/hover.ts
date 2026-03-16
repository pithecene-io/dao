import { EditorView } from "@codemirror/view";
import { getSource } from "./editor";

interface HoverResponse {
  name: string;
  kind: string;
  type: string;
}

let hoverTooltip: HTMLElement | null = null;
let hoverSeq = 0;
let hoverTimer: ReturnType<typeof setTimeout> | null = null;

const HOVER_DEBOUNCE_MS = 100;

function esc(text: string): string {
  const div = document.createElement("div");
  div.textContent = text;
  return div.innerHTML;
}

export function initHover(view: EditorView): void {
  const container = view.dom;

  container.addEventListener("mousemove", (e: MouseEvent) => {
    if (hoverTimer) clearTimeout(hoverTimer);
    hoverTimer = setTimeout(() => doHover(view, e), HOVER_DEBOUNCE_MS);
  });

  container.addEventListener("mouseleave", hideTooltip);
}

async function doHover(view: EditorView, e: MouseEvent): Promise<void> {
  const pos = view.posAtCoords({ x: e.clientX, y: e.clientY });
  if (pos === null) {
    hideTooltip();
    return;
  }

  const seq = ++hoverSeq;
  const source = getSource();

  try {
    const resp = await fetch("/api/hover", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ source, offset: pos }),
    });

    // Discard stale response.
    if (seq !== hoverSeq) return;

    if (!resp.ok) {
      hideTooltip();
      return;
    }

    const data: HoverResponse | null = await resp.json();
    if (seq !== hoverSeq) return;

    if (!data) {
      hideTooltip();
      return;
    }

    showTooltip(e.clientX, e.clientY, data);
  } catch {
    if (seq === hoverSeq) hideTooltip();
  }
}

function showTooltip(x: number, y: number, data: HoverResponse): void {
  if (!hoverTooltip) {
    hoverTooltip = document.createElement("div");
    hoverTooltip.className = "dao-hover-tooltip";
    document.body.appendChild(hoverTooltip);
  }

  let content = `<span class="hover-kind">${esc(data.kind)}</span> <strong>${esc(data.name)}</strong>`;
  if (data.type) {
    content += `<br><span class="hover-type">${esc(data.type)}</span>`;
  }

  hoverTooltip.innerHTML = content;
  hoverTooltip.style.left = `${x + 12}px`;
  hoverTooltip.style.top = `${y + 12}px`;
  hoverTooltip.style.display = "block";
}

function hideTooltip(): void {
  if (hoverTooltip) {
    hoverTooltip.style.display = "none";
  }
}
