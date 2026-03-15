import type { RunResponse } from "./types";
import { getSource } from "./editor";
import { renderDiagnostics } from "./diagnostics";

let runInFlight = false;

/** Compile and execute the current editor source. */
export async function doRun(): Promise<void> {
  if (runInFlight) return;
  runInFlight = true;

  const btn = document.getElementById("run-btn") as HTMLButtonElement;
  const output = document.getElementById("console-output")!;
  btn.disabled = true;
  btn.textContent = "⏳ Running…";
  output.className = "";
  output.textContent = "Compiling…";

  const source = getSource();

  try {
    const resp = await fetch("/api/run", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ source }),
    });

    if (!resp.ok) {
      output.className = "console-error";
      output.textContent = `Server error: ${resp.status}`;
      return;
    }

    const data: RunResponse = await resp.json();

    // Always update diagnostics (clears stale errors on success).
    renderDiagnostics(data.diagnostics || []);

    if (data.exit_code === -1) {
      output.className = "console-error";
      output.textContent =
        data.diagnostics && data.diagnostics.length > 0
          ? "Compilation failed. See diagnostics."
          : "Compilation failed (internal error).";
      return;
    }

    // Build console text.
    let text = "";
    if (data.stdout) text += data.stdout;
    if (data.stderr) {
      if (text) text += "\n";
      text += data.stderr;
    }

    if (data.exit_code !== 0) {
      if (text) text += "\n";
      text += `\nProcess exited with code ${data.exit_code}`;
      output.className = "console-error";
    } else {
      output.className = "console-ok";
    }

    output.textContent = text || "(no output)";
  } catch (err) {
    output.className = "console-error";
    output.textContent = `Run failed: ${(err as Error).message}`;
  } finally {
    btn.disabled = false;
    btn.textContent = "▶ Run";
    runInFlight = false;
  }
}
