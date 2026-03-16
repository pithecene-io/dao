import { EditorView } from "@codemirror/view";
import { getSource } from "./editor";

interface HoverResponse {
  name: string;
  kind: string;
  type: string;
}

let hoverTooltip: HTMLElement | null = null;

function esc(text: string): string {
  const div = document.createElement("div");
  div.textContent = text;
  return div.innerHTML;
}

export function initHover(view: EditorView): void {
  const container = view.dom;

  container.addEventListener("mousemove", async (e: MouseEvent) => {
    const pos = view.posAtCoords({ x: e.clientX, y: e.clientY });
    if (pos === null) {
      hideTooltip();
      return;
    }

    const source = getSource();

    try {
      const resp = await fetch("/api/hover", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ source, offset: pos }),
      });

      if (!resp.ok) {
        hideTooltip();
        return;
      }

      const data: HoverResponse | null = await resp.json();
      if (!data) {
        hideTooltip();
        return;
      }

      showTooltip(e.clientX, e.clientY, data);
    } catch {
      hideTooltip();
    }
  });

  container.addEventListener("mouseleave", hideTooltip);
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
